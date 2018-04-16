#include "server.h"

//Server socket structures
static int server_socketfd;
static struct sockaddr_in server_addr;                     //"struct sockaddr_in" can be casted as "struct sockaddr" for binding

//socket structures for the current client being served
static int client_socketfd;
static struct sockaddr_in client_addr;
static int client_addr_leng;

//epoll structures for handling multiple clients 
static struct epoll_event events[MAX_EPOLL_EVENTS];
static int epoll_fd;  

//Receive buffers
static char *buffer;
static size_t buffer_size = BUFSIZE;

//Server variables
unsigned int last_userid = 0;     


/******************************/
/*          Helpers           */
/******************************/                      

static int register_fd_with_epoll(int socketfd)
{
    struct epoll_event new_event;
    const int event_flags = EPOLLIN;
    
    new_event.events = event_flags;
    new_event.data.fd = socketfd;

    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socketfd, &new_event) < 0) 
    {
        perror("Failed to register the new client socket with epoll!\n");
        close(socketfd);
        return 0;
    }

    return 1;
}


static void disconnect_client(int client_socketfd)
{
    if(epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_socketfd, NULL) < 0) 
        perror("Failed to unregister the disconnected client from epoll!\n");
    
    close(client_socketfd);
}

/******************************/
/*     Basic Send/Receive     */
/******************************/


static unsigned int send_msg(int socket, char* buffer, size_t size)
{
    int bytes = send(socket, buffer, size, 0);
    if(bytes < 0)
    {
        perror("Failed to send greeting message to client\n");
        disconnect_client(socket);
        return 0;
    }
    return bytes;
}

static unsigned int recv_msg(int socket, char* buffer, size_t size)
{
    int bytes = recv(socket, buffer, BUFSIZE, 0);
    if(bytes < 0)
    {
        perror("Failed to receive from client. Disconnecting...\n");
        disconnect_client(socket);
        return 0;
    }

    //Client has disconnected unexpectedly
    else if(bytes == 0)
    {
        printf("Unexpected disconnect from client %s:%d : \n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        disconnect_client(socket);
        return 0;
    }

    return bytes;
}


/******************************/
/*      Client Operations     */
/******************************/

static inline int parse_client_command()
{
    int bytes; 

    //If the client requested to close the connection
    if(strncmp(buffer, "!register", 9) == 0)
    {
        char username[32];

        sscanf(buffer, "!register:username=%s", username);  
        printf("Registering user \"%s\" with userid %d\n", username, ++last_userid);
        sprintf(buffer, "!regreply:username=%s,userid=%u", username, last_userid);
        
        if(!send_msg(client_socketfd, buffer, BUFSIZE))
            return 0;
    }

    else if(strcmp(buffer, "!close") == 0)
    {
        printf("Closing connection with client %s on port %d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        disconnect_client(client_socketfd);
        return -1;
    }
    else
    {
        printf("Invalid command \"%s\"\n", buffer);
        return 0;
    }
        
    return 1;
}


static inline int handle_new_connection()
{
    
    client_addr_leng = sizeof(struct sockaddr_in);
    client_socketfd = accept(server_socketfd, (struct sockaddr*) &client_addr, &client_addr_leng);
    if(client_socketfd < 0)
    {
        perror("Error accepting client!\n");
        return 0;
    }
    printf("Accepted client %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

    //Register the new client's FD into epoll's event list, and mark it as nonblocking
    fcntl(client_socketfd, F_SETFL, O_NONBLOCK);
    if(!register_fd_with_epoll(client_socketfd))
        return 0;    

    //Send a greeting to the client
    if(!send_msg(client_socketfd, "Hello World!", 13))
        return 0;
    
    return 1;
} 

static inline int handle_client_msg()
{
    int bytes, retval;
    
    getsockname(client_socketfd, (struct sockaddr*) &client_addr, &client_addr_leng);
    bytes = recv_msg(client_socketfd, buffer, BUFSIZE);
    if(!bytes)
        return 0;
    
    printf("Received %d bytes from %s:%d : ", bytes, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
    printf("%s\n", buffer);

    //Parse as a command if message begins with '!'
    if(buffer[0] == '!')
        if(parse_client_command() < 0)
            return 0;
            
    //Reply back to client
    if(!send_msg(client_socketfd, "Received.", 10))
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
                client_socketfd = events[i].data.fd;
                handle_client_msg();
            }
                
        }
    }
}



void server(const char* ipaddr, const int port)
{
    struct epoll_event new_event;

    buffer = malloc(BUFSIZE * sizeof(char));
    
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


    /*Setup a listener on the socket*/
    if(listen(server_socketfd, MAX_CONNECTION_BACKLOG) < 0)
    {
        perror("Failed to listen to the socket!\n");
        return;
    }


    /*Setup epoll to allow multiplexed IO to serve multiple clients*/
    epoll_fd = epoll_create1(0);
    if(epoll_fd < 0)
    {
        perror("Failed to create epoll!\n");
        return;
    }


    /*Register the server socket to the epoll list, and also mark it as nonblocking*/
    fcntl(server_socketfd, F_SETFL, O_NONBLOCK);
    if(!register_fd_with_epoll(server_socketfd))
            return;   
    

    /*Register stdin (fd = 0) to the epoll list*/
    if(!register_fd_with_epoll(0))
        return;   


    /*Begin handling requests*/
    server_main_loop();
}