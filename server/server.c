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
size_t buffer_size = BUFSIZE;

//Event Timers
TimerEvent *timers = NULL; 
int timers_epollfd;  
pthread_mutex_t client_lock = PTHREAD_MUTEX_INITIALIZER;        //Locked when a thread is currently working on some client requests
pthread_t timer_event_thread;


//Keeping track of clients
Client *active_connections = NULL;                  //Hashtable of all active client sockets (key = socketfd)
User *active_users = NULL;                          //Hashtable of all active users (key = username), mapped to their client descriptors
Group *groups = NULL;                               //Hashtable of all user created private chatrooms (key = groupname)                      
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

void kill_connection(int socketfd)
{
    if(epoll_ctl(connections_epollfd, EPOLL_CTL_DEL, socketfd, NULL) < 0) 
        perror("Failed to unregister the disconnected client from epoll!");

    close(socketfd);
}

static inline void cleanup_unregistered_connection(Client *c)
{
    printf("Dropping unregistered connection...\n");
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
        
    //Leave participating chat groups
    disconnect_client_group_cleanup(c);
        
    //Free up resources used by the user
    sprintf(disconnect_msg, "!useroffline=%s", c->username);
    send_bcast(disconnect_msg, strlen(disconnect_msg)+1, 1, 0);

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
    

    //Free objects used by this connection
    HASH_DEL(active_connections, c);
    free(c);
}


void cleanup_timer_event(TimerEvent *event)
{
    if(epoll_ctl(timers_epollfd, EPOLL_CTL_DEL, event->timerfd, NULL) < 0) 
        perror("Failed to unregister expired timer from epoll!");
    close(event->timerfd);

    HASH_DEL(timers, event);
    free(event);
}

static void exit_cleanup()
{
    close(server_socketfd);
}



/******************************/
/*     Basic Send/Receive     */
/******************************/

//TODO: Handle partial sends
/*unsigned int send_msg_direct(int socketfd, char* buffer, size_t size)
{
    int sent, sent_total = 0;

    while(sent_total < size)
    {
        sent = send(socketfd, buffer, size, 0);

        if(!sent)
            return sent;  
        sent_total += sent;
    }
    return sent_total;
}*/

unsigned int send_msg_direct(int socketfd, char* buffer, size_t size)
{
    return send(socketfd, buffer, size, 0);
}

unsigned int send_msg(Client *c, char* buffer, size_t size)
{
    int bytes;

    if(size == 0)
    {
        printf("Cannot send this message. size=0 \n");
        return 0;
    }
    else if(size > MAX_MSG_LENG)
    {
        printf("Cannot send this message. Size too big\n");
        return 0;
    }

    
    bytes = send_msg_direct(c->socketfd, buffer, size);
    if(bytes <= 0)
    {
        perror("Failed to send greeting message to client");
        disconnect_client(c);
        return 0;
    }
    
    return bytes;
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


static void send_long_msg()
{
    int remaining_size = current_client->pending_size - current_client->pending_processed;
    int bytes;

    //Start of a new long send. Register the client's FD to signal on EPOLLOUT
    if(current_client->pending_processed == 0)
        update_epoll_events(connections_epollfd, current_client->socketfd, CLIENT_EPOLL_DEFAULT_EVENTS | EPOLLOUT);
     
    //Send the next chunk to the client
    bytes = send_msg_direct(current_client->socketfd, &current_client->pending_buffer[current_client->pending_processed], (remaining_size >= LONG_RECV_PAGE_SIZE)? LONG_RECV_PAGE_SIZE:remaining_size);
    printf("Sending chunk (%d): %.*s\n", bytes, bytes, &current_client->pending_buffer[current_client->pending_processed]);

    //Check exit conditions
    if(bytes <= 0)
    {
        perror("Failed to send long message");
        printf("Sent %zu\\%zu bytes to client \"%s\" before failure.\n", current_client->pending_processed, current_client->pending_size, current_client->username);
    }
    else
    {
        current_client->pending_processed += bytes;
        printf("Sent %zu\\%zu bytes to client \"%s\"\n", current_client->pending_processed, current_client->pending_size, current_client->username);

        if(current_client->pending_processed < current_client->pending_size)
            return;
    }

    //Cleanup resources once long sent has completed/failed
    free(current_client->pending_buffer);
    current_client->pending_buffer = NULL;
    current_client->pending_size = 0;
    current_client->pending_processed = 0;

    //Remove the EPOLLOUT notification once long send has completed
    update_epoll_events(connections_epollfd, current_client->socketfd, CLIENT_EPOLL_DEFAULT_EVENTS);
}


void send_new_long_msg(char* buffer, size_t size)
{
    char* newbuffer;
    size_t header_size;

    //if(size < BUFSIZE)
    if(size < LONG_RECV_PAGE_SIZE)
    {
        send_msg(current_client, buffer, size);
        return;
    }

    newbuffer = malloc(size + 48);

    //Add the longmsg header and total message size before the message
    sprintf(newbuffer, "!longmsg=%zu ", size);
    header_size = strlen(newbuffer);
    strcat(newbuffer, buffer);

    printf("New Long Message (h: %zu m: %zu): \"%s\"\n", header_size, size, newbuffer);

    //Start the operation
    current_client->pending_buffer = newbuffer;
    current_client->pending_size = size + header_size;
    current_client->pending_processed = 0;
    send_long_msg();
}


unsigned int recv_msg(Client *c, char* buffer, size_t size)
{
    int bytes = recv(c->socketfd, buffer, BUFSIZE, 0);
    if(bytes < 0)
    {
        perror("Failed to receive from client. Disconnecting...");
        printf("Client: %s\n", c->username);
        disconnect_client(c);
        return 0;
    }

    //Client has disconnected unexpectedly
    else if(bytes == 0)
    {
        printf("Unexpected disconnect from client %s:%d : \n", inet_ntoa(c->sockaddr.sin_addr), ntohs(c->sockaddr.sin_port));
        disconnect_client(c);
        return 0;
    }

    return bytes;
}



/******************************/
/*      Client Operations     */
/******************************/


static inline int register_client_connection()
{
    char *username = malloc(USERNAME_LENG+1);
    User *result;

    char *new_username;
    int duplicates = 0, max_duplicates_allowed;

    if(current_client->connection_type != UNREGISTERED_CONNECTION)
    {
        printf("Connection already registered. Type: %d.\n", current_client->connection_type);
        disconnect_client(current_client);
        return 0;
    }

    sscanf(buffer, "!register:username=%s", username);
    if(!name_is_valid(username))
        return 0;
    
    //Check if the requested username is a duplicate. Append a number (up to 999) after the username if it already exists 
    HASH_FIND_STR(active_users, username, result);
    while(result != NULL)
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
        HASH_FIND_STR(active_users, new_username, result);
    }

    if(duplicates)
    {
        printf("Found %d other clients with the same username. Changed username to \"%s\".\n", duplicates, new_username);
        free(username);
        username = new_username;
    }

    //Register the client's requested username
    result = malloc(sizeof(User));
    result->c = current_client;
    strcpy(result->username, username);
    HASH_ADD_STR(active_users, username, result);

    //Update the client descriptor
    current_client->connection_type = USER_CONNECTION;
    strcpy(current_client->username, username);
    free(username);

    printf("Registering user \"%s\"\n", current_client->username);
    sprintf(buffer, "!regreply:username=%s", current_client->username);
    send_msg(current_client, buffer, strlen(buffer)+1);
    
    sprintf(buffer, "!useronline=%s", current_client->username);
    send_bcast(buffer, strlen(buffer)+1, 1, 0);
    ++total_users;

    printf("User \"%s\" has connected. Total users: %d\n", current_client->username, total_users); 
    return 1;
}


static inline int client_pm()
{
    char *target_username;
    char *target_msg;
    char pmsg[MAX_MSG_LENG];
    User *target;

    //Find the occurance of the first space
    target_msg = strchr(buffer, ' ');
    if(!target_msg)
    {
        printf("Invalid username specified, or no message specified\n");
        return 0;
    }

    //Seperate the target's name and message from the buffer
    target_username = &buffer[1];
    target_msg[0] = '\0';              //Mark the space separating the username and message as NULL, so we can directly read the username from the pointer
    target_msg += sizeof(char);        //Increment the message pointer by 1 to skip to the actual message

    //Find if anyone with the requested username is online
    HASH_FIND_STR(active_users, target_username, target);
    if(!target)
    {
        printf("User \"%s\" not found\n", target_username);
        send_msg(current_client, "UserNotFound", 13);
        return 0;
    }

    //Forward message to the target
    sprintf(pmsg, "%s (PM): %s", current_client->username, target_msg);
    if(!send_msg(target->c, pmsg, strlen(pmsg)+1))
        return 0;

    //Echo back to the sender
    sprintf(pmsg, "%s (PM to %s): %s", current_client->username, target_username, target_msg);
    if(!send_msg(current_client, pmsg, strlen(pmsg)+1))
        return 0;
    
    return 1;
}


static inline int userlist()
{
    char* userlist_msg;
    size_t userlist_size = 0;
    User *curr, *temp;

    char group_name[USERNAME_LENG+1];

    //Is the user requesting a userlist of a group, or a userlist of everyone online?
    if(strncmp(buffer, "!userlist,group=", 16) == 0)
    {
        sscanf(buffer, "!userlist,group=%s", group_name);
        return userlist_group(group_name);
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
    
    send_new_long_msg(userlist_msg, userlist_size);
    free(userlist_msg);

    return total_users;
}

static inline int parse_client_command()
{
    /*Connection related commands*/

    if(strncmp(buffer, "!register:", 10) == 0)
        return register_client_connection();
    

    else if(strncmp(buffer, "!xfersend=", 10) == 0)
        return register_send_transfer_connection();
    
    else if(strncmp(buffer, "!xferrecv=", 10) == 0)
        return register_recv_transfer_connection();

    else if(strcmp(buffer, "!close") == 0)
    {
        printf("Closing connection with client %s on port %d\n", inet_ntoa(current_client->sockaddr.sin_addr), ntohs(current_client->sockaddr.sin_port));
        disconnect_client(current_client);
        return -1;
    }



    /*Group related Commands*/

    else if(strncmp(buffer, "!userlist", 9) == 0)
        return userlist();

    else if(strncmp(buffer, "!newgroup=", 10) == 0)
        return create_new_group();

    else if(strncmp(buffer, "!joingroup=", 11) == 0)
        return join_group();

    else if(strncmp(buffer, "!leavegroup=", 12) == 0)
        return leave_group();

    else if(strncmp(buffer, "!invitegroup=", 13) == 0)
        return invite_to_group();

    else if(strncmp(buffer, "!kickgroup=", 11) == 0)
        return kick_from_group();


    /*File Transfer Commands*/
    else if(strncmp(buffer, "!sendfile=", 10) == 0)
        return new_client_transfer();

    else if(strncmp(buffer, "!acceptfile=", 12) == 0)
        return accepted_file_transfer();

    else if(strncmp(buffer, "!rejectfile=", 12) == 0)
        return rejected_file_transfer();

    else if(strncmp(buffer, "!cancelfile", 11) == 0)
        return user_cancelled_transfer();

    /*File Transfer for Groups*/
    else if(strncmp(buffer, "!filelist=", 10) == 0)
        return group_filelist();

    else if(strncmp(buffer, "!putfile=", 9) == 0)
        return put_new_file_to_group();

    else if(strncmp(buffer, "!getfile=", 9) == 0)
        return get_new_file_from_group();
    
    
    else
    {
        printf("Invalid command \"%s\"\n", buffer);
        send_msg(current_client, "InvalidCmd.", 12);
        return 0;
    }
        
    return 1;
}



/******************************/
/*    Core Server Operations  */
/******************************/

static inline int handle_new_connection()
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
    
    return 1;
} 

static inline int handle_client_msg()
{
    int bytes;

    //If this connection is a transfer connection, forward the data contained directly to its target
    if(current_client->connection_type == TRANSFER_CONNECTION)
        return client_data_forward_sender_ready(); 
    
    //Otherwise, read the regular user's message into the global buffer for further processing
    bytes = recv_msg(current_client, buffer, BUFSIZE);
    if(!bytes)
        return 0;
    
    printf("Received %d bytes from %s:%d : ", bytes, inet_ntoa(current_client->sockaddr.sin_addr), ntohs(current_client->sockaddr.sin_port));
    printf("%.*s\n", bytes, buffer);

    //Parse as a command if message begins with '!'
    if(buffer[0] == '!')
        return parse_client_command();

    //Private messaging between two users if message starts with "@". Group message if message starts with "@@"
    else if(buffer[0] == '@')
    {
        if(buffer[1] == '@')
            return group_msg();
        else
            return client_pm();
    }

    //Broadcast regular messages to all other active clients
    else
        send_bcast(buffer, buffer_size, 0, 1);
        
    return 1;
}


static inline int handle_stdin()
{
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

            if(!read(current_timer_event->timerfd, &timer_retval, sizeof(uint64_t)))
                perror("Failed to read timer event.");
            else
                printf("Event from user %s. Count=%lu\n", current_timer_event->c->username, timer_retval);
            
            //TODO: Notify sender of expiry
            transfer_invite_expired(current_timer_event->c);

            //Cleanup and delete this event once it has occured
            cleanup_timer_event(current_timer_event);
            current_timer_event = NULL;
        }

        pthread_mutex_unlock(&client_lock);
    }
}


static inline void server_main_loop()
{
    struct epoll_event events[MAX_EPOLL_EVENTS];
    int ready_count, i;

    //Spawn a new thread that monitors timer events
    if(pthread_create(&timer_event_thread, NULL, (void*) &handle_timer_events, NULL) != 0)
    {
        printf("Failed to create timer event thread\n");
        return;
    }
    
    //Continuously handle network events
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
                    handle_client_msg();

                //Handle EPOLLOUT (ready for writing) if the client has pending long messages
                else if(events[i].events & EPOLLOUT)
                {
                    if(current_client->pending_size > 0)
                        send_long_msg();

                    else if(current_client->connection_type == TRANSFER_CONNECTION)
                        client_data_forward_recver_ready();
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

    /*Begin listening for incoming connections on the server socket*/
    if(listen(server_socketfd, MAX_CONNECTION_BACKLOG) < 0)
    {
        perror("Failed to listen to the socket!");
        return;
    }

    printf("Listening for new client connections at %s:%u...\n", ipaddr_used, ntohs(server_addr.sin_port));

    /*Begin handling requests*/
    server_main_loop();
}



#undef MAX_CONNECTION_BACKLOG
#undef MAX_EPOLL_EVENTS
#undef CLIENT_EPOLL_DEFAULT_EVENTS