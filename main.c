#include <stdio.h>
#include <string.h>

#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h> 

#define MAX_CONNECTION_BACKLOG 8 

const char* ipaddr = "127.0.0.1";
const int port = 6996;

#define bufsize 256
char buffer[bufsize];

void server()
{
    //Server socket structures
    int server_socketfd;
    struct sockaddr_in server_addr;                     //"struct sockaddr_in" can be casted as "struct sockaddr" for binding

    //Client socket structures
    int client_socketfd;
    struct sockaddr_in client_addr;
    int client_addr_leng;

    int retval;


    //Create a TCP server socket
    server_socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_socketfd < 0)
    {
        perror("Error creating socket!\n");
        return;
    }

    //Bind specified IP/Port to the socket
    memset(&server_addr, 0, sizeof(struct sockaddr_in));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    //server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_addr.s_addr = inet_addr(ipaddr);

    if(bind(server_socketfd, (struct sockaddr*) &server_addr, sizeof(struct sockaddr)) < 0)
    {
        perror("Failed to bind socket socket!\n");
        return;
    }

    //Start server loop. The current implementation allows the server to handle one client at a time
    while(1)
    {
        //Listen for any incoming connections (blocking)
        listen(server_socketfd, MAX_CONNECTION_BACKLOG);

        //When a connection arrives, accept it
        client_addr_leng = sizeof(struct sockaddr_in);
        client_socketfd = accept(server_socketfd, (struct sockaddr*) &client_addr, &client_addr_leng);
        if(client_socketfd < 0)
        {
            perror("Error accepting client!\n");
            return;
        }

        printf("Got client!\n");
        printf("Accepted client %s on port %d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        //Send a greeting to the client
        retval = send(client_socketfd, "Hello World!", 13, 0);
        if(retval < 0)
        {
            perror("Failed to send greeting message to client\n");
            return;
        }

        //Terminate the connection with the client
        close(client_socketfd);
    }
    
    

    
}

void client()
{
    int my_socketfd;                    //My (client) socket fd
    struct sockaddr_in server_addr;     //Server's information struct

    int retval;

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

    //Receive a greeting message from the server
    retval = recv(my_socketfd, &buffer, bufsize, 0);
    if(retval < 0)
    {
        perror("Failed to receive greeting message from server\n");
        return;
    }

    printf("%s\n", buffer);

    //Terminate the connection with the server
    close(my_socketfd);
}


int main(int argc, char *argv[])
{
    if(argc < 1)
    {
        printf("Not enough args...\n");
        return 0;
    }

    if(strcmp(argv[1], "-s") == 0)
    {
        printf("Running as server...\n");
        server();
    }  
    else
    {
        printf("Running as Client...\n");
        client();
    }
        
}