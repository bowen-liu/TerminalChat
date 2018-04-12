#include "shared.h"

extern char buffer[bufsize];            //in main.c

void client(const char* ipaddr, const int port)
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

    //Send messages to the host forever
    while(1)
    {
        scanf("%512s", buffer);

        retval = send(my_socketfd, &buffer, bufsize, 0);
        if(retval < 0)
        {
            perror("Failed to receive greeting message from server\n");
            return;
        }

        if(strcmp(buffer, "!close") == 0)
        {
            printf("Closing connection!\n");
            break;
        }

        retval = recv(my_socketfd, &buffer, bufsize, 0);
        if(retval < 0)
        {
            perror("Failed to receive greeting message from server\n");
            return;
        }
        printf("Server replied: %s\n", buffer);
    }

    //Terminate the connection with the server
    close(my_socketfd);
}