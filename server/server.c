#include "server.h"
#include <pthread.h>

#define MAX_CONNECTION_BACKLOG 8
#define MAX_EPOLL_EVENTS    32 

//Server socket structures
int server_socketfd;
struct sockaddr_in server_addr;                     //"struct sockaddr_in" can be casted as "struct sockaddr" for binding
int connections_epollfd;  

//Receive buffers
char *buffer;
char *msg_target;                                   //msg_target and msg_body points to sections in buffer. Do not write to these!
char *msg_body;

//Event Timers
TimerEvent *timers = NULL; 
int timers_epollfd;  
pthread_mutex_t client_lock = PTHREAD_MUTEX_INITIALIZER;        //Locked when a thread is currently working on some client requests
pthread_t timer_event_thread;


//Keeping track of clients
Client *active_connections = NULL;                  //Hashtable of all active client sockets (key = socketfd)
User *active_users = NULL;                          //Hashtable of all active users (key = username), mapped to their client descriptors
unsigned int total_users = 0;    

//Client/Event being served right now
Client *current_client;                             //Descriptor for the client being serviced right now
TimerEvent *current_timer_event;



/******************************/
/*          Helpers           */
/******************************/                 

User* get_current_client_user()
{
    User* user;
    HASH_FIND_STR(active_users, current_client->username, user);
    return user;
}

void cleanup_timer_event(TimerEvent *event)
{
    if(epoll_ctl(timers_epollfd, EPOLL_CTL_DEL, event->timerfd, NULL) < 0) 
        perror("Failed to unregister expired timer from epoll!");
    close(event->timerfd);

    HASH_DEL(timers, event);
    free(event);
}

void kill_connection(int socketfd)
{
    if(epoll_ctl(connections_epollfd, EPOLL_CTL_DEL, socketfd, NULL) < 0) 
        perror("Failed to unregister the disconnected client from epoll!");

    close(socketfd);
}

static inline void cleanup_unregistered_connection(Client *c)
{
    printf("Dropping unregistered connection from %s:%u...\n", inet_ntoa(c->sockaddr.sin_addr), ntohs(c->sockaddr.sin_port));
    kill_connection(c->socketfd);
}


static inline void cleanup_user_connection(Client *c)
{
    char disconnect_msg[BUFSIZE];
    User *user;

    printf("Disconnecting \"%s\"...\n", c->username);
    
    //If the disconnecting user has an ongoing transfer connection, kill it first.
    cancel_user_transfer(c);

    //Close the main user's connection
    kill_connection(c->socketfd);
    printf("Closed user connection for \"%s\"\n", c->username);
    c->socketfd = 0;
        
    //Leave participating chat groups
    disconnect_client_group_cleanup(c);
        
    //Free up resources used by the user
    /*sprintf(disconnect_msg, "!useroffline=%s", c->username);
    send_bcast(disconnect_msg, strlen(disconnect_msg)+1, 1, 0);*/

    HASH_FIND_STR(active_users, c->username, user);
    HASH_DEL(active_users, user);
    free(user);
    
    --total_users;
}


void disconnect_client(Client *c)
{    
    //Check if the connection is for a registered client
    if(c->connection_type == UNREGISTERED_CONNECTION)
        cleanup_unregistered_connection(c);
        
    //Check if the connection is for a file transfer
    else if(current_client->connection_type == TRANSFER_CONNECTION)
        cleanup_transfer_connection(c);

    /*The connection is for a regular user*/
    else
        cleanup_user_connection(c); 

    //Destroy the connection's idle timer, if it exists
    if(c->idle_timer)
        cleanup_timer_event(c->idle_timer);
    

    //Free objects used by this connection
    HASH_DEL(active_connections, c);
    free(c);
}


static void exit_cleanup()
{
    close(server_socketfd);
}



/******************************/
/*     Basic Send/Receive     */
/******************************/

static unsigned int transfer_next_pending(Client *c)
{
    int retval;

    retval = transfer_next_common(c->socketfd, &c->pending_msg);
    if(retval <= 0)
        disconnect_client(c);
    
    //Remove the EPOLLOUT notification once long send has completed
    if(c->pending_msg.pending_op == NO_XFER_OP)
    {
        update_epoll_events(connections_epollfd, c->socketfd, CLIENT_EPOLL_DEFAULT_EVENTS);
        printf("Completed partial transfer.\n");
    }
    
    return retval;
}

unsigned int send_msg_direct(int socketfd, char* buffer, size_t size)
{
    return send(socketfd, buffer, size, 0);
}

unsigned int send_msg(Client *c, char* buffer, size_t size)
{
    int retval;
    
    retval = send_msg_common(c->socketfd, buffer, size, &c->pending_msg);
    
    if(retval < 0)
        disconnect_client(c);
    else if(retval == 0)
        return 0;

    //Start of a new long send. Register the client's FD to signal on EPOLLOUT
    if(c->pending_msg.pending_op == SENDING_OP)
        update_epoll_events(connections_epollfd, c->socketfd, CLIENT_EPOLL_DEFAULT_EVENTS | EPOLLOUT);

    return retval;
}

unsigned int send_bcast(char* buffer, size_t size, int is_control_msg, int include_current_client)
{
    int count = 0;
    char bcast_msg[BUFSIZE];
    User *curr, *temp;

    if(is_control_msg)
        sprintf(bcast_msg, "%s", buffer);
    else
        sprintf(bcast_msg, "%s: %s", current_client->username, buffer);
    
    //Send the message in the buffer to every active client
    HASH_ITER(hh, active_users, curr, temp)
    {
        if(!include_current_client && curr->c->socketfd == current_client->socketfd)
            continue;

        send_msg(curr->c, bcast_msg, strlen(bcast_msg)+1);
        ++count;
    }
    
    return count;
}


unsigned int recv_msg(Client *c, char* buffer, size_t size)
{
    int retval; 

    retval = recv_msg_common(c->socketfd, buffer, size, &c->pending_msg);
    
    if(retval < 0)
        disconnect_client(c);
    else if(retval == 0)
        return 0;

    return retval;
}



/******************************/
/*      Client Operations     */
/******************************/

static inline int register_client_connection()
{
    char *username = malloc(USERNAME_LENG+1);
    User *registered_user;

    char *new_username;
    int duplicates = 0, max_duplicates_allowed;

    if(current_client->connection_type != UNREGISTERED_CONNECTION)
    {
        printf("Connection already registered. Type: %d.\n", current_client->connection_type);
        send_msg(current_client, "AlreadyRegistered", 18);
        return 0;
    }

    sscanf(buffer, "!register:username=%s", username);
    if(!name_is_valid(username))
    {
        send_msg(current_client, "InvalidUsername", 16);
        disconnect_client(current_client);
        return 0;
    }

    //Cancel the unregistered idle timer
    cleanup_timer_event(current_client->idle_timer);
    current_client->idle_timer = NULL;
    
    //Check if the requested username is a duplicate. Append a number (up to 999) after the username if it already exists 
    HASH_FIND_STR(active_users, username, registered_user);
    while(registered_user != NULL)
    {
        if(duplicates == 0)
        {
            new_username = malloc(USERNAME_LENG+1);

            //How many reminaing free bytes in the username can be used for appending numbers?
            max_duplicates_allowed = USERNAME_LENG - strlen(username) - 1;

            //Determine the largest numerical value (up to 999) can be used from the free bytes
            if(max_duplicates_allowed > 3)
                max_duplicates_allowed = 999;
            else if(max_duplicates_allowed == 2)
                max_duplicates_allowed = 99;
            else if(max_duplicates_allowed == 1)
                max_duplicates_allowed = 9;
            else
                max_duplicates_allowed = 0;
        }

        if(++duplicates > max_duplicates_allowed)
        {
            printf("The username \"%s\" cannot support further clients.\n", username);
            send_msg(current_client, "InvalidUsername", 16);
            disconnect_client(current_client);
            return 0;
        }

        //Test if the new name with a newly appended value is used
        sprintf(new_username, "%s_%d", username, duplicates);
        HASH_FIND_STR(active_users, new_username, registered_user);
    }

    if(duplicates)
    {
        printf("Found %d other clients with the same username. Changed username to \"%s\".\n", duplicates, new_username);
        free(username);
        username = new_username;
    }

    //Register the client's requested username
    registered_user = malloc(sizeof(User));
    registered_user->c = current_client;
    strcpy(registered_user->username, username);
    HASH_ADD_STR(active_users, username, registered_user);

    //Update the client descriptor
    current_client->connection_type = USER_CONNECTION;
    strcpy(current_client->username, username);
    free(username);

    printf("Registering user \"%s\"\n", current_client->username);
    sprintf(buffer, "!regreply:username=%s", current_client->username);
    send_msg(current_client, buffer, strlen(buffer)+1);

    //Announce to all online users that a new user has joined
    /*sprintf(buffer, "!useronline=%s", current_client->username);
    send_bcast(buffer, strlen(buffer)+1, 1, 0);*/
    ++total_users;

    printf("User \"%s\" has connected. Total users: %d\n", current_client->username, total_users); 
    
    return 1;
}


static inline int client_pm()
{
    char pmsg[MAX_MSG_LENG + 2*USERNAME_LENG];
    User *target;

    //Do nothing if there is no message body
    if(!msg_body)
        return 0;
    ++msg_target;

    //Find if anyone with the requested username is online
    HASH_FIND_STR(active_users, msg_target, target);
    if(!target)
    {
        printf("User \"%s\" not found\n", msg_target);
        send_msg(current_client, "UserNotFound", 13);
        return 0;
    }

    //Forward message to the target
    sprintf(pmsg, "%s (PM): %s", current_client->username, msg_body);
    if(send_msg(target->c, pmsg, strlen(pmsg)+1) <= 0)
        return 0;

    //Echo back to the sender
    sprintf(pmsg, "%s (PM to %s): %s", current_client->username, msg_target, msg_body);
    send_msg(current_client, pmsg, strlen(pmsg)+1);
    
    return 1;
}


static inline int userlist()
{
    char* userlist_msg;
    size_t userlist_size = 0;
    User *curr, *temp;

    //Is the user requesting a userlist of a group, or a userlist of everyone online?
    if(msg_target && strncmp(msg_target, "@@", 2) == 0)
    {
        msg_target += 2;
        return userlist_group(msg_target);
    }

    userlist_msg = malloc(total_users * (USERNAME_LENG+1 + 128));
    sprintf(userlist_msg, "!userlist=%d", total_users);
    userlist_size = strlen(userlist_msg);

    //Iterate through the list of active usernames and append them to the buffer one at a time
    HASH_ITER(hh, active_users, curr, temp)
    {
        strcat(userlist_msg, ",");
        strcat(userlist_msg, curr->username);
    }
    userlist_size = strlen(userlist_msg) + 1;
    userlist_msg[userlist_size] = '\0';
    
    send_msg(current_client, userlist_msg, userlist_size);
    free(userlist_msg);

    return total_users;
}

static inline int parse_client_command()
{
    /*Connection related commands*/

    if(strcmp(msg_body, "!close") == 0)
    {
        printf("Closing connection with client %s on port %d\n", inet_ntoa(current_client->sockaddr.sin_addr), ntohs(current_client->sockaddr.sin_port));
        disconnect_client(current_client);
        return -1;
    }


    /*Group related Commands*/

    else if(strncmp(msg_body, "!userlist", 9) == 0)
        return userlist();

    else if(strncmp(msg_body, "!newgroup", 9) == 0)
        return create_new_group();

    else if(strncmp(msg_body, "!joingroup ", 11) == 0)
        return join_group();

    else if(strncmp(msg_body, "!leavegroup ", 12) == 0)
        return leave_group();

    else if(strncmp(msg_body, "!invitegroup,", 13) == 0)
        return invite_to_group();

    else if(strncmp(msg_body, "!kickgroup,", 10) == 0)
        return kick_from_group();


    /*File Transfer Commands*/
    else if(strncmp(msg_body, "!sendfile=", 10) == 0)
        return new_client_transfer();

    else if(strncmp(msg_body, "!acceptfile=", 12) == 0)
        return accepted_file_transfer();

    else if(strncmp(msg_body, "!rejectfile=", 12) == 0)
        return rejected_file_transfer();

    else if(strncmp(msg_body, "!cancelfile", 11) == 0)
        return user_cancelled_transfer();

    /*File Transfer for Groups*/
    else if(strcmp(msg_body, "!filelist") == 0)
        return group_filelist();

    else if(strncmp(msg_body, "!putfile=", 9) == 0)
        return put_new_file_to_group();

    else if(strncmp(msg_body, "!getfile ", 9) == 0)
        return get_new_file_from_group();
    
    
    else
    {
        printf("Invalid command \"%s\"\n", msg_body);
        send_msg(current_client, "InvalidCmd.", 12);
        return 0;
    }
        
    return 1;
}



/******************************/
/*    Core Server Operations  */
/******************************/

static int handle_new_connection()
{    
    Client *new_client = calloc(1, sizeof(Client));
    current_client = new_client;
    
    new_client->connection_type = UNREGISTERED_CONNECTION;
    new_client->sockaddr_leng = sizeof(struct sockaddr_in);
    new_client->socketfd = accept(server_socketfd, (struct sockaddr*) &new_client->sockaddr, &new_client->sockaddr_leng);

    if(new_client->socketfd < 0)
    {
        perror("Error accepting client!");
        free(new_client);
        return 0;
    }
    printf("Accepted new connection %s:%d (fd=%d)\n", inet_ntoa(new_client->sockaddr.sin_addr), ntohs(new_client->sockaddr.sin_port), new_client->socketfd);

    //Add the client into active_connections, and use its socketfd as the key.
    HASH_ADD_INT(active_connections, socketfd, new_client);

    //Register the new client's FD into epoll's event list, and mark it as nonblocking
    fcntl(new_client->socketfd, F_SETFL, O_NONBLOCK);
    if(!register_fd_with_epoll(connections_epollfd, new_client->socketfd, CLIENT_EPOLL_DEFAULT_EVENTS))
        return 0;    

    //Send a greeting to the client
    if(!send_msg(new_client, "Hello World!", 13))
        return 0;

    //Set a timer that disconnects the unregistered client after a certain period of no registration
    new_client->idle_timer = calloc(1, sizeof(TimerEvent));
    new_client->idle_timer->event_type = EXPIRING_UNREGISTERED_CONNECTION;
    new_client->idle_timer->c = new_client;
    new_client->idle_timer->timerfd = create_timerfd(UNREGISTERED_CONNECTION_TIMEOUT, 0, timers_epollfd);

    if(!new_client->idle_timer->timerfd)
        return 0;
    HASH_ADD_INT(timers, timerfd, new_client->idle_timer);
    
    return 1;
} 

static int handle_client_msg(int use_pending_msg)
{
    int bytes;

    //If this connection is a transfer connection, forward the data contained directly to its target
    if(current_client->connection_type == TRANSFER_CONNECTION)
        return client_data_forward_sender_ready(); 
    
    //If this connection is not yet registered, the client must register itself before anything else can be done.
    else if(current_client->connection_type == UNREGISTERED_CONNECTION)
    {
        bytes = recv_msg(current_client, buffer, BUFSIZE);
        
        if(!bytes)
            return 0;
        
        printf("Received %d bytes from %s:%d: \"%.*s\"\n", 
                bytes, inet_ntoa(current_client->sockaddr.sin_addr), ntohs(current_client->sockaddr.sin_port), bytes, buffer);

        if(strncmp(buffer, "!register:", 10) == 0)
            return register_client_connection();

        else if(strncmp(buffer, "!xfersend=", 10) == 0)
            return register_send_transfer_connection();
        
        else if(strncmp(buffer, "!xferrecv=", 10) == 0)
            return register_recv_transfer_connection();

        else
            disconnect_client(current_client);   
        return 0;
    }

    //Read regular user's message
    if(use_pending_msg)
    {
        //will pending_msg.pending_buffer ever be bigger than the server's default buffer?
        bytes = current_client->pending_msg.pending_size;
        if(bytes > BUFSIZE)
        {
            printf("Too big for message buffer: %d bytes.\n", bytes);
            clean_pending_msg(&current_client->pending_msg);
            return 0;
        }

        memcpy(buffer, current_client->pending_msg.pending_buffer, current_client->pending_msg.pending_size);
        clean_pending_msg(&current_client->pending_msg);
    }
    else
    {
        bytes = recv_msg(current_client, buffer, BUFSIZE);
        if(!bytes)
            return 0;
        
        //Message has not been received completely yet
        if(current_client->pending_msg.pending_op != NO_XFER_OP)
            return 1;
    }

    printf("Received from %s: \"%.*s\"\n", current_client->username, bytes, buffer);
    seperate_target_command(buffer, &msg_target, &msg_body);

    //Parse as a command if message begins with '!'
    if(msg_body && msg_body[0] == '!')
        return parse_client_command();

    //Private messaging between two users if message starts with "@". Group message if message starts with "@@"
    else if(msg_target && msg_target[0] == '@')
    {
        if(msg_target[1] == '@')
            return group_msg();
        return client_pm();
    }

    //If a regular message has no target, broadcast it to the lobby group 
    send_lobby(current_client, buffer, strlen(buffer)+1);
    
    return 1;
}


static int handle_stdin()
{
    size_t buffer_size = BUFSIZE;
    int bytes; 

    //Read from stdin and remove the newline character
    bytes = getline(&buffer, &buffer_size, stdin);
    if (buffer[bytes-1] == '\n') 
    {
        buffer[bytes-1] = '\0';
        --bytes;
    }
    
    printf("stdin: %s\n", buffer);

    return bytes;
}

static void handle_timer_events()
{
    struct epoll_event events[MAX_EPOLL_EVENTS];
    int ready_count, i;
    uint64_t timer_retval;
    
    while(1)
    {
        //Wait until epoll has detected some event in the registered timer event fd's
        ready_count = epoll_wait(timers_epollfd, events, MAX_EPOLL_EVENTS, -1);
        if(ready_count < 0)
        {
            perror("epoll_wait failed!");
            return;
        }
        
        //Got some timer events ready
        pthread_mutex_lock(&client_lock);

        for(i=0; i<ready_count; i++)
        {
            HASH_FIND_INT(timers, &events[i].data.fd, current_timer_event);
            if(!current_timer_event)
            {
                printf("Event is no longer present.\n");
                continue;
            }

            //Read the timer counter to silence epoll
            if(!read(current_timer_event->timerfd, &timer_retval, sizeof(uint64_t)))
                perror("Failed to read timer event.");
            
            //Which event type is this?
            if(current_timer_event->event_type == EXPIRING_UNREGISTERED_CONNECTION)
            {
                if(current_timer_event->c->connection_type == UNREGISTERED_CONNECTION)
                {                    
                    disconnect_client(current_timer_event->c);
                    current_timer_event = NULL;
                }
            }
            else if (current_timer_event->event_type == EXPIRING_TRANSFER_REQ)
            {
                transfer_invite_expired(current_timer_event->c);
                current_timer_event = NULL;
            }
                
            
            else
                printf("Unknown timer event.\n");
            

            //Cleanup and delete this event once it has occured
            if(current_timer_event)
            {
                cleanup_timer_event(current_timer_event);
                current_timer_event = NULL;
            }
            
        }

        pthread_mutex_unlock(&client_lock);
    }
}


static inline void server_main_loop()
{
    struct epoll_event events[MAX_EPOLL_EVENTS];
    int ready_count, i;
    
    while(1)
    {
        //Wait until epoll has detected some event in the registered socket fd's
        ready_count = epoll_wait(connections_epollfd, events, MAX_EPOLL_EVENTS, -1);
        if(ready_count < 0)
        {
            perror("epoll_wait failed!");
            return;
        }

        //Got some network events ready
        pthread_mutex_lock(&client_lock);

        for(i=0; i<ready_count; i++)
        {
            //When the administrator enters a command from stdin
            if(events[i].data.fd == 0)
                handle_stdin();
            
            //When a new connection arrives to the server socket, accept it
            else if(events[i].data.fd == server_socketfd)
                handle_new_connection();

            //When an event is occuring on an existing client connection
            else
            {                
                HASH_FIND_INT(active_connections, &events[i].data.fd, current_client);
                if(!current_client)
                {
                    printf("Connection (fd=%d) is no longer active.\n", events[i].data.fd);
                    continue;
                }

                //Handle EPOLLRDHUP: the client has closed its connection
                if(events[i].events & EPOLLRDHUP)
                {
                    disconnect_client(current_client);
                    continue;
                }    
                
                //Handles EPOLLIN (ready for reading)
                else if(events[i].events & EPOLLIN)
                {
                    if(current_client->pending_msg.pending_op == RECVING_OP)
                    {
                        transfer_next_pending(current_client);
                        
                        //Check if the entire message has been received
                        if(current_client->pending_msg.pending_op == NO_XFER_OP)
                            handle_client_msg(1);
                        continue;
                    }
                        
                    handle_client_msg(0);
                }
                    

                //Handle EPOLLOUT (ready for writing) if the client has pending long messages
                else if(events[i].events & EPOLLOUT)
                {
                    if(current_client->connection_type == TRANSFER_CONNECTION)
                        client_data_forward_recver_ready();
                    
                    else if(current_client->pending_msg.pending_op == SENDING_OP)
                    {
                        transfer_next_pending(current_client);
                        //printf("\nBuffer AFTER sent: \"%s\"\n", current_client->pending_msg.pending_buffer);

                        if(current_client->pending_msg.pending_op == NO_XFER_OP)
                        {
                            clean_pending_msg(&current_client->pending_msg);
                            continue;
                        }
                    } 
                    else
                        printf("Unknown EPOLLOUT\n");
                }
            }    
        }

        pthread_mutex_unlock(&client_lock);
    }
}



void server(const char* hostname, const unsigned int port)
{   
    char ipaddr_used[INET_ADDRSTRLEN], port_str[8];

    atexit(exit_cleanup);

    /*Initialize network buffer*/
    buffer = calloc(BUFSIZE, sizeof(char));

    /*Setup epoll to allow multiplexed IO to serve multiple clients, and to use timerfd's*/
    connections_epollfd = epoll_create1(0);
    timers_epollfd = epoll_create1(0);

    if(connections_epollfd < 0 || timers_epollfd < 0)
    {
        perror("Failed to create epoll!");
        return;
    }
    
    /*Create a TCP server socket*/
    server_socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_socketfd < 0)
    {
        perror("Error creating socket!");
        return;
    }

    memset(&server_addr, 0, sizeof(struct sockaddr_in));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    //Resolve the specified hostname for binding, if specified
    if(hostname)
    {
        sprintf(port_str, "%u", port);
        if(!hostname_to_ip(hostname, port_str, ipaddr_used))
            return;
        server_addr.sin_addr.s_addr = inet_addr(ipaddr_used);
    }
    else
        server_addr.sin_addr.s_addr = INADDR_ANY;

    //Record the string of the IP address we chose
    inet_ntop(AF_INET, &server_addr.sin_addr, ipaddr_used, INET_ADDRSTRLEN);

    //Allow the socket to be bound to the same address (in case if it crashes or didn't have a clean exit)
    setsockopt(server_socketfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));

    /*Bind specified IP/Port to the socket*/
    if(bind(server_socketfd, (struct sockaddr*) &server_addr, sizeof(struct sockaddr)) < 0)
    {
        printf("Failed to bind socket at %s:%u. \n", ipaddr_used, ntohs(server_addr.sin_port));
        perror("");
        return;
    }

    /*Register the server socket to the epoll list, and also mark it as nonblocking*/
    fcntl(server_socketfd, F_SETFL, O_NONBLOCK);
    if(!register_fd_with_epoll(connections_epollfd, server_socketfd, EPOLLIN))
        return;   
    
    /*Register stdin (fd = 0) to the epoll list*/
    if(!register_fd_with_epoll(connections_epollfd, 0, EPOLLIN))
        return;   

    /*Initialize other server components before listening for connections*/
    create_lobby_group();

    /*Begin listening for incoming connections on the server socket*/
    if(listen(server_socketfd, MAX_CONNECTION_BACKLOG) < 0)
    {
        perror("Failed to listen to the socket!");
        return;
    }

    printf("Listening for new client connections at %s:%u...\n", ipaddr_used, ntohs(server_addr.sin_port));

    /*Spawn a new thread that monitors timer events*/
    if(pthread_create(&timer_event_thread, NULL, (void*) &handle_timer_events, NULL) != 0)
    {
        printf("Failed to create timer event thread\n");
        return;
    }

    /*Begin handling requests*/
    server_main_loop();
}



#undef MAX_CONNECTION_BACKLOG
#undef MAX_EPOLL_EVENTS
#undef CLIENT_EPOLL_DEFAULT_EVENTS