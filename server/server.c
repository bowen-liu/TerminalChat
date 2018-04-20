#include "server.h"

#define MAX_CONNECTION_BACKLOG 8
#define MAX_EPOLL_EVENTS    32 

//Server socket structures
static int server_socketfd;
static struct sockaddr_in server_addr;                     //"struct sockaddr_in" can be casted as "struct sockaddr" for binding

//epoll event structures for handling multiple clients 
static struct epoll_event events[MAX_EPOLL_EVENTS];
static int epoll_fd;  

//Receive buffers
static char *buffer;
static size_t buffer_size = BUFSIZE;

//Keeping track of clients
Client *active_clients = NULL;                  //Hashes client's FD to their descriptors
Username_Map *active_clients_names = NULL;      //Hashes each unique name to their corresponding descriptors

Client *current_client;                         //Descriptor for the client being serviced right now
unsigned int total_users = 0;    



/******************************/
/*          Helpers           */
/******************************/                 

static unsigned int send_bcast(char* buffer, size_t size, int is_control_msg, int include_current_client);

static void disconnect_client(Client *c)
{
    char disconnect_msg[BUFSIZE];
    Username_Map *username_map;
    
    if(epoll_ctl(epoll_fd, EPOLL_CTL_DEL, c->socketfd, NULL) < 0) 
        perror("Failed to unregister the disconnected client from epoll!\n");

    close(c->socketfd);

    //Check if the connection is for a registered client
    if(strlen(c->username) == 0)
    {
        printf("Dropping unregistered connection...\n");
        HASH_DEL(active_clients, c);
        free(c);

        return;
    }
        
    //Free up resources used by the user
    sprintf(disconnect_msg, "!useroffline=%s", c->username);
    printf("Disconnecting \"%s\"\n", c->username);
    send_bcast(disconnect_msg, strlen(disconnect_msg)+1, 1, 0);

    HASH_FIND_STR(active_clients_names, c->username, username_map);
    HASH_DEL(active_clients_names, username_map);
    free(username_map);
    
    HASH_DEL(active_clients, c);
    free(c);

    --total_users;
}

/******************************/
/*     Basic Send/Receive     */
/******************************/


static unsigned int send_msg(Client *c, char* buffer, size_t size)
{
    int bytes;

    if(size == 0)
        return 0;
        
    else if(size > MAX_MSG_LENG)
    {
        printf("Cannot send this message. Size too big\n");
        return 0;
    }
    
    bytes = send(c->socketfd, buffer, size, 0);
    if(bytes < 0)
    {
        perror("Failed to send greeting message to client\n");
        disconnect_client(c);
        return 0;
    }
    return bytes;
}


static unsigned int send_bcast(char* buffer, size_t size, int is_control_msg, int include_current_client)
{
    int count = 0;
    char bcast_msg[BUFSIZE];
    Client *curr, *temp;

    if(is_control_msg)
        sprintf(bcast_msg, "%s", buffer);
    else
        sprintf(bcast_msg, "%s: %s", current_client->username, buffer);
    
    //Send the message in the buffer to every active client
    HASH_ITER(hh, active_clients, curr, temp)
    {
        if(!include_current_client && curr->socketfd == current_client->socketfd)
            continue;
        
        send_msg(curr, bcast_msg, strlen(bcast_msg)+1);
        ++count;
    }
    
    return count;
}



static void send_long_msg()
{
    int recv_page_size = 32;
    int remaining_size = current_client->pending_size - current_client->pending_processed;

    
    int bytes = send(current_client->socketfd, &current_client->pending_buffer[current_client->pending_processed], (remaining_size >= recv_page_size)? recv_page_size:remaining_size, 0);

    printf("Sent chunk (%d): %.*s\n", bytes, bytes, &current_client->pending_buffer[current_client->pending_processed]);

    if(bytes <= 0)
    {
        perror("Failed to send long message\n");
        printf("Sent %zu\\%zu bytes to client \"%s\" before failure.\n", current_client->pending_processed, current_client->pending_size, current_client->username);
        
        free(current_client->pending_buffer);
        current_client->pending_buffer = NULL;
        current_client->pending_size = 0;
        current_client->pending_processed = 0;

        return;
    }

    current_client->pending_processed += bytes;
    printf("Sent %zu\\%zu bytes to client \"%s\"\n", current_client->pending_processed, current_client->pending_size, current_client->username);
    

    if(current_client->pending_processed == current_client->pending_size)
    {
        free(current_client->pending_buffer);
        current_client->pending_buffer = NULL;
        current_client->pending_size = 0;
        current_client->pending_processed = 0;
    }
}


static unsigned int recv_msg(Client *c, char* buffer, size_t size)
{
    int bytes = recv(c->socketfd, buffer, BUFSIZE, 0);
    if(bytes < 0)
    {
        perror("Failed to receive from client. Disconnecting...\n");
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
/*  Client/Client Operations  */
/******************************/

static inline int client_pm()
{
    char target_username[USERNAME_LENG+1];
    char msg[MAX_MSG_LENG];
    Username_Map *target;

    int i;

    printf("%s\n", buffer);

    //Find the occurance of the first space
    for(i=1; i<strlen(buffer); i++)
        if(buffer[i] == ' ')
            break;
    
    //No valid message found
    if(i == strlen(buffer) || (i-1) > USERNAME_LENG)
    {
        printf("Invalid username specified, or no message specified\n");
        return 0;
    }

    //Seperate the target's name and message from the buffer
    strncpy(target_username, &buffer[1], i-1);
    target_username[i-1] = '\0';
    strcpy(msg, &buffer[i+1]);
  
    //Find if anyone with the requested username is online
    HASH_FIND_STR(active_clients_names, target_username, target);
    if(!target)
    {
        printf("User \"%s\" not found\n", target_username);
        send_msg(current_client, "UserNotFound", 13);
        return 0;
    }

    //Forward message to the target
    sprintf(buffer, "%s (PM): %s", current_client->username, msg);
    if(!send_msg(target->c, buffer, strlen(buffer)+1))
        return 0;

    //Echo back to the sender
    sprintf(buffer, "%s (PM to %s): %s", current_client->username, target_username, msg);
    if(!send_msg(current_client, buffer, strlen(buffer)+1))
        return 0;
    
    return 1;
}



/******************************/
/*  Client/Server Operations  */
/******************************/

static inline int parse_client_command()
{
    int bytes; 

    //If the client requested to close the connection
    if(strncmp(buffer, "!register", 9) == 0)
    {  
        char username[32];
        Username_Map *result;

        sscanf(buffer, "!register:username=%s", username);  
        
        //Verifies if the username is already in use
        HASH_FIND_STR(active_clients_names, username, result);
        if(result != NULL)
        {
            printf("Username already exists!\n");
            return 0;
        }

        //Register the client's requested username
        result = malloc(sizeof(Username_Map));
        strcpy(current_client->username, username);
        strcpy(result->username, username);
        result->c = current_client;
        HASH_ADD_STR(active_clients_names, username, result);


        printf("Registering user \"%s\"\n", current_client->username);
        sprintf(buffer, "!regreply:username=%s", current_client->username);

        if(!send_msg(current_client, buffer, strlen(buffer)+1))
            return 0;
        
        sprintf(buffer, "!useronline=%s", current_client->username);
        send_bcast(buffer, strlen(buffer)+1, 1, 0);

        ++total_users;
    }

    else if(strcmp(buffer, "!close") == 0)
    {
        printf("Closing connection with client %s on port %d\n", inet_ntoa(current_client->sockaddr.sin_addr), ntohs(current_client->sockaddr.sin_port));
        disconnect_client(current_client);
        return -1;
    }

    else if(strcmp(buffer, "!userlist") == 0)
    {
        char testmsg[101] = "1111111111222222222233333333334444444444555555555566666666667777777777888888888899999999990000000000";
        
        current_client->pending_size = 126;
        current_client->pending_buffer = malloc(current_client->pending_size);

        sprintf(current_client->pending_buffer, "!longmsg=126 !userlist=1,%s", testmsg);

        send_long_msg();
        return 1;
    }

    else
    {
        printf("Invalid command \"%s\"\n", buffer);
        send_msg(current_client, "InvalidCmd.", 12);
        return 0;
    }
        
    return 1;
}


static inline int handle_client_msg()
{
    int bytes, retval;
    
    bytes = recv_msg(current_client, buffer, BUFSIZE);
    if(!bytes)
        return 0;
    
    printf("Received %d bytes from %s:%d : ", bytes, inet_ntoa(current_client->sockaddr.sin_addr), ntohs(current_client->sockaddr.sin_port));
    printf("%s\n", buffer);

    //Parse as a command if message begins with '!'
    if(buffer[0] == '!')
        return parse_client_command();

    //Direct messaging to another user/group
    else if(buffer[0] == '@')
        return client_pm();

    //Broadcast regular messages to all other active clients
    else
        send_bcast(buffer, buffer_size, 0, 1);
        
    return 1;
}


static inline int handle_new_connection()
{    
    Client *new_client = calloc(1, sizeof(Client));
    current_client = new_client;
    
    new_client->username[0] = '\0';                             //Empty username indicates an unregistered connection
    new_client->sockaddr_leng = sizeof(struct sockaddr_in);
    new_client->socketfd = accept(server_socketfd, (struct sockaddr*) &new_client->sockaddr, &new_client->sockaddr_leng);

    if(new_client->socketfd < 0)
    {
        perror("Error accepting client!\n");
        free(new_client);
        return 0;
    }
    printf("Accepted client %s:%d\n", inet_ntoa(new_client->sockaddr.sin_addr), ntohs(new_client->sockaddr.sin_port));

    //Add the client into active_clients, and use its socketfd as the key.
    HASH_ADD_INT(active_clients, socketfd, new_client);


    //Register the new client's FD into epoll's event list (edge triggered), and mark it as nonblocking
    fcntl(new_client->socketfd, F_SETFL, O_NONBLOCK);
    //if(!register_fd_with_epoll(epoll_fd, new_client->socketfd, EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET))
    if(!register_fd_with_epoll(epoll_fd, new_client->socketfd, EPOLLIN | EPOLLOUT | EPOLLRDHUP))
        return 0;    

    //Send a greeting to the client
    if(!send_msg(new_client, "Hello World!", 13))
        return 0;
    
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





/******************************/
/*     Server Entry Point     */
/******************************/

static inline void server_main_loop()
{
    int ready_count, i;
    
    while(1)
    {
        //Wait until epoll has detected some event in the registered fd's
        ready_count = epoll_wait(epoll_fd, events, MAX_EPOLL_EVENTS, -1);
        if(ready_count < 0)
        {
            perror("epoll_wait failed!\n");
            return;
        }


        for(i=0; i<ready_count; i++)
        {
            //When the administrator enters a command from stdin
            if(events[i].data.fd == 0)
            {
                handle_stdin();
            }
            
            //When a new connection arrives to the server socket, accept it
            else if(events[i].data.fd == server_socketfd)
            {
                handle_new_connection();
            }

            //When an event is occuring on an existing client connection
            else
            {                
                HASH_FIND_INT(active_clients, &events[i].data.fd, current_client);

                //Handle EPOLLRDHUP: the client has closed its connection unexpectedly
                if(events[i].events & EPOLLRDHUP)
                {
                    printf("Client \"%s\" has closed its connection.\n", current_client->username);
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
                }
            }
                
        }
    }
}



void server(const char* ipaddr, const int port)
{   
    /*Initialize network buffer*/
    buffer = calloc(BUFSIZE, sizeof(char));

    /*Setup epoll to allow multiplexed IO to serve multiple clients*/
    epoll_fd = epoll_create1(0);
    if(epoll_fd < 0)
    {
        perror("Failed to create epoll!\n");
        return;
    }
    
    /*Create a TCP server socket*/
    server_socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_socketfd < 0)
    {
        perror("Error creating socket!\n");
        return;
    }

    /*Bind specified IP/Port to the socket*/
    memset(&server_addr, 0, sizeof(struct sockaddr_in));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    //server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_addr.s_addr = inet_addr(ipaddr);

    if(bind(server_socketfd, (struct sockaddr*) &server_addr, sizeof(struct sockaddr)) < 0)
    {
        perror("Failed to bind socket!\n");
        return;
    }


    /*Register the server socket to the epoll list, and also mark it as nonblocking*/
    fcntl(server_socketfd, F_SETFL, O_NONBLOCK);
    if(!register_fd_with_epoll(epoll_fd, server_socketfd, EPOLLIN))
            return;   
    
    /*Register stdin (fd = 0) to the epoll list*/
    if(!register_fd_with_epoll(epoll_fd, 0, EPOLLIN))
        return;   

    /*Begin listening for incoming connections on the server socket*/
    if(listen(server_socketfd, MAX_CONNECTION_BACKLOG) < 0)
    {
        perror("Failed to listen to the socket!\n");
        return;
    }

    /*Begin handling requests*/
    server_main_loop();
}