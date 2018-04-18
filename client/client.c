#include "client.h"

#define MAX_EPOLL_EVENTS    32 

//Socket for communicating with the server
static int my_socketfd;                    //My (client) socket fd
static struct sockaddr_in server_addr;     //Server's information struct

//epoll event structures for handling multiple clients 
static struct epoll_event events[MAX_EPOLL_EVENTS];
static int epoll_fd;

//Receive buffers
static char *buffer;
static size_t buffer_size = BUFSIZE;

//User info
static char* userName;


/******************************/
/*     Basic Send/Receive     */
/******************************/

static unsigned int send_msg(int socket, char* buffer, size_t size)
{
    int bytes;

    if(size == 0)
        return 0;

    else if(size > MAX_MSG_LENG)
    {
        printf("Cannot send this message. Size too big\n");
        return 0;
    }
    
    bytes = send(socket, buffer, size, 0);
    if(bytes < 0)
    {
        perror("Failed to sent message to the server...\n");
        return 0;
    }

    return bytes;
}

static unsigned int recv_msg(int socket, char* buffer, size_t size)
{
    int bytes = recv(socket, buffer, size, 0);
    if(bytes < 0)
    {
        perror("Failed to receive message from server\n");
        return 0;
    }
    else if(bytes == 0)
    {
        perror("Server has disconnected unexpectedly...\n");
        return 0;
    }

    return bytes;
}


/******************************/
/*  Server Control Messages   */
/******************************/

static void parse_control_message()
{
    char cmd_username[USERNAME_LENG];

    if(strncmp("!useroffline=", buffer, 13) == 0)
    {
        sscanf(buffer, "!useroffline=%s", cmd_username);
        printf("User \"%s\" has disconnected.\n", cmd_username);
    }

    else if(strncmp("!useronline=", buffer, 12) == 0)
    {
        sscanf(buffer, "!useronline=%s", cmd_username);
        printf("User \"%s\" has connected.\n", cmd_username);
    }

    else
        printf("Received invalid control message \"%s\"\n", buffer);
}


/******************************/
/*      Client Operations     */
/******************************/

static void register_with_server()
{
    int bytes;
    
    //Receive a greeting message from the server
    if(!recv_msg(my_socketfd, buffer, BUFSIZE))
        return;
    printf("%s\n", buffer);

    //Register my desired username
    printf("Registering username \"%s\"...\n", userName);
    sprintf(buffer, "!register:username=%s", userName);
    if(!send_msg(my_socketfd, buffer, strlen(buffer)+1))
        return;

    //Parse registration reply from server
    if(!recv_msg(my_socketfd, buffer, BUFSIZE))
        return;
    
    sscanf(buffer, "!regreply:username=%s", userName);
    printf("Registered with server as \"%s\"\n", userName);
}


static int handle_user_command()
{
    //Todo: escape commands like "!register"
    
    return 1;
}



/******************************/
/*     Client Entry Point     */
/******************************/

static inline void client_main_loop()
{
     int bytes;
     int ready_count, i;
     
     //Send messages to the host forever
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
                //Read from stdin and remove the newline character
                bytes = getline(&buffer, &buffer_size, stdin);
                remove_newline(buffer);
                --bytes;

                //Transmit the line read from stdin to the server
                if(!send_msg(my_socketfd, buffer, strlen(buffer)+1))
                    return;


                if(strcmp(buffer, "!close") == 0)
                {
                    printf("Closing connection!\n");
                    break;
                }
            }
            
            //Message from Server
            else
            {                
                //Anticipate a reply from the server
                if(!recv_msg(my_socketfd, buffer, BUFSIZE))
                    return;

                if(buffer[0] == '!')
                    parse_control_message();
                else
                    printf("%s\n", buffer);
            }
        }
    }

}



void client(const char* ipaddr, const int port,  char *username)
{
    int retval;

    buffer = calloc(BUFSIZE, sizeof(char));
    userName = username;
    printf("Username: %s\n", username);

    /*Setup epoll to allow multiplexed IO to serve multiple clients*/
    epoll_fd = epoll_create1(0);
    if(epoll_fd < 0)
    {
        perror("Failed to create epoll!\n");
        return;
    }

    //Create a TCP socket 
    my_socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if(my_socketfd < 0)
    {
        perror("Error creating socket!\n");
        return;
    }

    //Fill in the server's information
    memset(&server_addr, 0, sizeof(struct sockaddr_in));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ipaddr);


    //Connect to the server
    if(connect(my_socketfd, (struct sockaddr*) &server_addr, sizeof(struct sockaddr)) < 0)
    {
        perror("Error connecting to the server!\n");
        return;
    }

    //Register with the server
    register_with_server();

    /*Register the server socket to the epoll list, and also mark it as nonblocking*/
    fcntl(my_socketfd, F_SETFL, O_NONBLOCK);
    if(!register_fd_with_epoll(epoll_fd, my_socketfd))
            return;   
    
    /*Register stdin (fd = 0) to the epoll list*/
    if(!register_fd_with_epoll(epoll_fd, 0))
        return; 


    client_main_loop();
   
    //Terminate the connection with the server
    close(my_socketfd);
}