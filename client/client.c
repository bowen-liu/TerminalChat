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
static size_t last_received;

static int pending_long_msg = 0;
static char *long_buffer;
static size_t expected_long_size;
static size_t received_long;

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
/*     Long Send/Receive     */
/******************************/

static void parse_control_message(char* buffer);
static void recv_long_msg()
{
    int recv_page_size = 32;
    int bytes;

    if(!pending_long_msg)
    {
        pending_long_msg = 1;
        received_long = last_received;
        long_buffer = malloc(expected_long_size);
        memcpy(long_buffer, buffer, last_received);

        printf("Initial chunk (%d): %.*s\n", received_long, received_long, long_buffer);
        return;
    }

    bytes = recv(my_socketfd, &long_buffer[received_long], recv_page_size, 0);

    if(bytes <= 0)
    {
        perror("Failed to receive long message from server.\n");
        printf("%zu\\%zu bytes received before failure.\n", received_long, expected_long_size);
        
        pending_long_msg = 0;
        expected_long_size = 0;
        received_long = 0;
        free(long_buffer);
        long_buffer = NULL;

        return;
    }

    printf("Received chunk (%d): %.*s\n", bytes, bytes, &long_buffer[received_long-1]);
    printf("Current Buffer: %.*s\n", received_long, long_buffer);

    received_long += bytes;
    printf("%zu\\%zu bytes received\n", received_long, expected_long_size);
    

    //The entire message has been received
    if(expected_long_size == received_long)
    {
        int i;

        printf("Completed long recv\n");

        //Start at the end of "!longmsg=" to find the first space
        for(i=10; i<expected_long_size; i++)        
        {
            if(long_buffer[i] == ' ')
                break;
        }
        
        parse_control_message(&long_buffer[i+1]);

        pending_long_msg = 0;
        expected_long_size = 0;
        received_long = 0;
        free(long_buffer);
        long_buffer = NULL;
    }
}


/******************************/
/*  Server Control Messages   */
/******************************/

static void parse_control_message(char* buffer)
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

    else if(strncmp("!longmsg=", buffer, 9) == 0)
    {   
        sscanf(buffer, "!longmsg=%zu ", &expected_long_size);
        printf("Expecting a long message from the server, size: %zu\n", expected_long_size);
        recv_long_msg();
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
                //Return to receiving a long message if it's still pending
                if(pending_long_msg == 1)
                {
                    recv_long_msg();
                    continue;
                }
                
                //Otherwise, receive a regular new message from the server
                last_received = recv_msg(my_socketfd, buffer, BUFSIZE);
                if(!last_received)
                    return;

                if(buffer[0] == '!')
                    parse_control_message(buffer);
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
    if(!register_fd_with_epoll(epoll_fd, my_socketfd, EPOLLIN))
            return;   
    
    /*Register stdin (fd = 0) to the epoll list*/
    if(!register_fd_with_epoll(epoll_fd, 0, EPOLLIN))
        return; 


    client_main_loop();
   
    //Terminate the connection with the server
    close(my_socketfd);
}