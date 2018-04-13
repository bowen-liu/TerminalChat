#include "client.h"


//Socket for communicating with the server
static int my_socketfd;                    //My (client) socket fd
static struct sockaddr_in server_addr;     //Server's information struct

//Receive buffers
static char *buffer;
static size_t buffer_size = BUFSIZE;

//User info
static unsigned int userID;
static char* userName;


static int handle_user_command()
{
    //Todo: escape commands like "!register"
    
    return 1;
}


static void client_main_loop()
{
     int bytes;
     
     //Send messages to the host forever
    while(1)
    {
        //Read from stdin and remove the newline character
        bytes = getline(&buffer, &buffer_size, stdin);
        if (buffer[bytes-1] == '\n') 
        {
            buffer[bytes-1] = '\0';
            --bytes;
        }

        //Transmit the line read from stdin to the server
        bytes = send(my_socketfd, buffer, BUFSIZE, 0);
        if(bytes < 0)
        {
            perror("Failed to receive greeting message from server\n");
            return;
        }

        if(strcmp(buffer, "!close") == 0)
        {
            printf("Closing connection!\n");
            break;
        }

        bytes = recv(my_socketfd, buffer, BUFSIZE, 0);
        if(bytes < 0)
        {
            perror("Failed to receive greeting message from server\n");
            return;
        }
        printf("Server replied: %s\n", buffer);
    }

}


static void register_with_server()
{
    int bytes;
    
    //Receive a greeting message from the server
    bytes = recv(my_socketfd, buffer, BUFSIZE, 0);
    if(bytes <= 0)
    {
        perror("Failed to send greeting message from server\n");
        return;
    }

    printf("%s\n", buffer);

    //Register my desired username
    printf("Registering username \"%s\"...\n", userName);
    sprintf(buffer, "!register:username=%s", userName);
    bytes = send(my_socketfd, buffer, BUFSIZE, 0);
    if(bytes <= 0)
    {
        perror("Failed to send to server\n");
        return;
    }


    //Parse registration reply from server
    bytes = recv(my_socketfd, buffer, BUFSIZE, 0);
    if(bytes <= 0)
    {
        perror("Failed to receive greeting message from server\n");
        return;
    }
    sscanf(buffer, "!regreply:username=%[^,],userid=%u", userName, &userID);
    printf("Registered with server as \"%s\", userid: %u\n", userName, userID);
}


void client(const char* ipaddr, const int port,  char *username)
{
    int retval;

    printf("Username: %s\n", username);

    buffer = malloc(BUFSIZE * sizeof(char));
    userName = username;

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
    client_main_loop();
   
    //Terminate the connection with the server
    close(my_socketfd);
}