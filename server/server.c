#include "server.h"


//Receive buffers
static char *buffer;
static size_t buffer_size = BUFSIZE;

//Server socket structures
static int server_socketfd;
static struct sockaddr_in server_addr;                     //"struct sockaddr_in" can be casted as "struct sockaddr" for binding

//socket structures for the current client being served
int client_socketfd;
struct sockaddr_in client_addr;
int client_addr_leng;

//epoll structures for handling multiple client_socketfd
static struct epoll_event events[MAX_EPOLL_EVENTS];
static int epoll_fd;  

//Server variables
unsigned int last_userid = 0;                           


/*static void print_client(int client_socketfd)
{
    struct sockaddr_in client_addr;
    int client_addr_leng;

    printf("%s:%d", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
}*/

static void disconnect_client(int client_socketfd)
{
    if(epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_socketfd, NULL) < 0) 
        perror("Failed to unregister the disconnected client from epoll!\n");
    
    close(client_socketfd);
}


static int parse_client_command()
{
    int bytes; 

    //If the client requested to close the connection
    if(strncmp(buffer, "!register", 9) == 0)
    {
        char username[32];

        sscanf(buffer, "!register:username=%s", username);  
        printf("Registering user \"%s\" with userid %d\n", username, ++last_userid);

        sprintf(buffer, "!regreply:username=%s,userid=%u", username, last_userid);
        bytes = send(client_socketfd, buffer, BUFSIZE, 0);
        if(bytes < 0)
        {
            perror("Failed to send greeting message to client\n");
            //disconnect_client(client_socketfd);
            return 0;
        }
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


static inline void server_main_loop()
{
    struct epoll_event new_event;

    int bytes, retval;
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
                //Read from stdin and remove the newline character
                bytes = getline(&buffer, &buffer_size, stdin);
                if (buffer[bytes-1] == '\n') 
                {
                    buffer[bytes-1] = '\0';
                    --bytes;
                }
                
                printf("stdin: %s\n", buffer);
            }
            
            //When a new connection arrives to the server socket, accept it
            else if(events[i].data.fd == server_socketfd)
            {
                client_addr_leng = sizeof(struct sockaddr_in);
                client_socketfd = accept(server_socketfd, (struct sockaddr*) &client_addr, &client_addr_leng);
                if(client_socketfd < 0)
                {
                    perror("Error accepting client!\n");
                    //continue;
                    return;
                }
                printf("Accepted client %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

                //Register the new client's FD into epoll's event list, and mark it as nonblocking
                fcntl(client_socketfd, F_SETFL, O_NONBLOCK);
                
                new_event.events = EPOLLIN;
                new_event.data.fd = client_socketfd;

                if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socketfd, &new_event) < 0) 
                {
                    perror("Failed to register the new client socket with epoll!\n");
                    //close(client_socketfd);
                    return;
                }


                //Send a greeting to the client
                bytes = send(client_socketfd, "Hello World!", 13, 0);
                if(bytes < 0)
                {
                    perror("Failed to send greeting message to client\n");
                    //disconnect_client(client_socketfd);
                    return;
                }
            }

            //When an event is occuring on an existing client connection
            else
            {                
                client_socketfd = events[i].data.fd;
                bytes = recv(client_socketfd, buffer, BUFSIZE, 0);

                //Load the client's information from the socket's FD
                getsockname(client_socketfd, (struct sockaddr*) &client_addr, &client_addr_leng);
                printf("Received %d bytes from %s:%d : ", bytes, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

                if(bytes == 0)
                    continue;
                if(bytes < 0)
                {
                    perror("Failed to receive from client\n");
                    //disconnect_client(client_socketfd);
                    return;
                }
                printf("%s\n", buffer);

                //Parse as a command if message begins with '!'
                if(buffer[0] == '!')
                    retval = parse_client_command();
                
                if(retval < 0)
                    continue;
                                
                //Reply back to client
                bytes = send(client_socketfd, "Received.", 10, 0);
                if(bytes < 0)
                {
                    perror("Failed to send message to client\n");
                    //disconnect_client(client_socketfd);
                    return;
                }
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
    fcntl(server_socketfd, F_SETFL, O_NONBLOCK);                    //Set server socket to nonblocking, as needed by epoll
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


    /*Register the server socket to the epoll list*/
    new_event.events = EPOLLIN;
    new_event.data.fd = server_socketfd;
    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_socketfd, &new_event) < 0) 
    {
        perror("Failed to register the server socket with epoll!\n");
        return;
    }


    /*Register stdin to the epoll list*/
    new_event.events = EPOLLIN;
    new_event.data.fd = 0;                                          //0 = fd for stdin
    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, 0, &new_event) < 0) 
    {
        perror("Failed to register stdin with epoll!\n");
        return;
    }


    /*Begin handling requests*/
    server_main_loop();
}