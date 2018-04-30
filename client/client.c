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
static Namelist* groups_joined;


//Other clients
static Member *online_members = NULL;
static unsigned int users_online = 0;


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
        perror("Failed to sent message to the server...");
        return 0;
    }

    return bytes;
}

static unsigned int recv_msg(int socket, char* buffer, size_t size)
{
    int bytes = recv(socket, buffer, size, 0);
    if(bytes < 0)
    {
        perror("Failed to receive message from server");
        return 0;
    }
    else if(bytes == 0)
    {
        perror("Server has disconnected unexpectedly...");
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
    int bytes, i;
    char header[48];
    char *longcmd;

    //Start of a new long recv operation
    if(!pending_long_msg)
    {
        sscanf(buffer, "%s ", header);
        sscanf(header, "!longmsg=%zu", &expected_long_size);
        expected_long_size += strlen(header) + 1;
        
        printf("Expecting a long message from the server, size: %zu\n", expected_long_size);
        
        pending_long_msg = 1;
        received_long = last_received;
        long_buffer = malloc(expected_long_size);
        memcpy(long_buffer, buffer, last_received);

        printf("Initial chunk (%zu): %.*s\n", received_long, (int)received_long, long_buffer);
        return;
    }

    bytes = recv(my_socketfd, &long_buffer[received_long], LONG_RECV_PAGE_SIZE, 0);

    //Check exit conditions
    if(bytes <= 0)
    {
        perror("Failed to receive long message from server.");
        printf("%zu\\%zu bytes received before failure.\n", received_long, expected_long_size);
    }
    else
    {
        printf("Received chunk (%d): %.*s\n", bytes, (int)bytes, &long_buffer[received_long-1]);

        received_long += bytes;
        printf("Current Buffer: %.*s\n", (int)received_long, long_buffer);
        printf("%zu\\%zu bytes received\n", received_long, expected_long_size);

        //Has the entire message been received?
        if(received_long < expected_long_size)
            return;
        printf("Completed long recv\n");

        //Extract the long command after the !longmsg header
        longcmd = strchr(long_buffer, ' ');
        if(longcmd)
            parse_control_message(&longcmd[1]);
        else
            printf("Long message is malformed or does not contain a further control message\n");
    }

    //Free resources used for the long recv
    free(long_buffer);
    pending_long_msg = 0;
    expected_long_size = 0;
    received_long = 0;
    long_buffer = NULL;
}



/******************************/
/*   Client-side Operations    */
/******************************/

static int register_with_server()
{
    int bytes;
    
    //Receive a greeting message from the server
    if(!recv_msg(my_socketfd, buffer, BUFSIZE))
        return 0;
    printf("%s\n", buffer);

    //Register my desired username
    printf("Registering username \"%s\"...\n", userName);
    sprintf(buffer, "!register:username=%s", userName);
    if(!send_msg(my_socketfd, buffer, strlen(buffer)+1))
        return 0;

    //Parse registration reply from server
    if(!recv_msg(my_socketfd, buffer, BUFSIZE))
        return 0;

    //Did we receive an anticipated !regreply?
    if(strncmp(buffer, "!regreply:username=", 19) == 0)
    {
        sscanf(&buffer[19], "%s", userName);
        printf("Registered with server as \"%s\"\n", userName);

        //Request a list of active users from the server. The response will be handled once main loop starts
        strcpy(buffer, "!userlist");
        if(!send_msg(my_socketfd, buffer, strlen(buffer)+1))
            return 0;
        return 1;
    }

    //Something went wrong and server doesn't want me to join :(
    printf("Server rejected registration: \"%s\"\n", buffer);
    return 0;
}


static int leave_group()
{
    char groupname[USERNAME_LENG+1];
    Namelist *current_group_name, *tmp_name;

    sscanf(buffer, "!leavegroup=%s", groupname);

    //Find the entry in the client's group_joined list and remove the entry
    LL_FOREACH_SAFE(groups_joined, current_group_name, tmp_name)
    {
        if(strcmp(groupname, current_group_name->name) == 0)
            break;
        else 
            current_group_name = NULL;
    }

    if(!current_group_name)
    {
        printf("You do not appear to be a member of the group \"%s\".\n", groupname);
        return 0;
    }

    printf("You have left the group \"%s\".\n", groupname);
    LL_DELETE(groups_joined, current_group_name);
    free(current_group_name);
    return 1;
}


static int handle_user_command()
{
    //Todo: escape commands like "!register"
    if(strcmp(buffer, "!close") == 0)
    {
        printf("Closing connection!\n");
        return -1;
    }
    else if(strncmp(buffer, "!leavegroup=", 12) == 0)
    {
        return leave_group();
    }

    return 1;
}


/******************************/
/*  Server Control Messages   */
/******************************/

static void user_offline()
{
    char target_username[USERNAME_LENG+1];
    Member *member;

    sscanf(buffer, "!useroffline=%s", target_username);
    printf("User \"%s\" has disconnected.\n", target_username);

    HASH_FIND_STR(online_members, target_username, member);
    if(!member)
    {
        printf("Could not find \"%s\" in the online list!\n", target_username);
        return;
    }
    HASH_DEL(online_members, member);
    --users_online;
}


static void user_online()
{
    Member *member = malloc(sizeof(Member));

    sscanf(buffer, "!useronline=%s", member->username);
    printf("User \"%s\" has connected.\n", member->username);
    HASH_ADD_STR(online_members, username, member);
    ++users_online;
}

//TODO: Maybe allow the user to choose to decline an invitation?
static void group_invited()
{
    char group_name[USERNAME_LENG+1], invite_sender[USERNAME_LENG+1];

    sscanf(buffer, "!groupinvite=%[^,],sender=%s", group_name, invite_sender);
    printf("You are being invited to the group \"%s\" by user \"%s\".\n", group_name, invite_sender);

    //Automatically accept it for now
    sprintf(buffer, "!joingroup=%s", group_name);
    send_msg(my_socketfd, buffer, strlen(buffer)+1);
}

static void group_joined()
{
    Namelist *groupname = malloc(sizeof(Namelist));
    char invite_sender[USERNAME_LENG+1];

    sscanf(buffer, "!groupjoined=%s", groupname->name);
    printf("You have joined the group \"%s\".\n", groupname->name);

    //Record the invited group into the list of participating groups
    LL_APPEND(groups_joined, groupname);
}

static void group_kicked()
{
    char group_name[USERNAME_LENG+1], kicked_by[USERNAME_LENG+1];
    Namelist *current_group_name, *tmp;

    sscanf(buffer, "!groupkicked=%[^,],by=%s", group_name, kicked_by);
    printf("You have been kicked out of the group \"%s\" by user \"%s\".\n", group_name, kicked_by);

    //Find the entry in the client's group_joined list and remove the entry
    LL_FOREACH_SAFE(groups_joined, current_group_name, tmp)
    {
        if(strcmp(group_name, current_group_name->name) == 0)
            break;
        else 
            current_group_name = NULL;
    }

    if(current_group_name)
    {
        LL_DELETE(groups_joined, current_group_name);
        free(current_group_name);
    }
    else
        printf("You do not appear to be a member of the group \"%s\".\n", group_name);
}


static void user_left_group()
{
    char groupname[USERNAME_LENG+1], username[USERNAME_LENG+1];

    sscanf(buffer, "!leftgroup=%[^,],user=%s", groupname, username);
    printf("User \"%s\" has left the group \"%s\".\n", username, groupname);
}


static void user_joined_group()
{
    char groupname[USERNAME_LENG+1], username[USERNAME_LENG+1];

    sscanf(buffer, "!joinedgroup=%[^,],user=%s", groupname, username);
    printf("User \"%s\" has joined the group \"%s\".\n", username, groupname);
}


static void parse_userlist()
{
    char group_name[USERNAME_LENG+1];
    char* newbuffer = buffer;
    char* token;
    Member *curr, *tmp;

    //Parse the header
    token = strtok(newbuffer, ",");
    if(!token)
        return;
    sscanf(token, "!userlist=%u", &users_online);

    //Is the following token the name of a group?
    token = strtok(NULL, ",");
    if(!token)
        return;

    //This is a group userlist
    if(strncmp(token, "group=", 6) == 0)
    {
        sscanf(token, "group=%[^,]", group_name);
        printf("%u users are currently online in the group \"%s\":\n", users_online, group_name);
        token = strtok(NULL, ",");
    }

    //This is a global userlist
    else
    {
        //Delete the old global userlist if it already exists
        if(online_members)
        {
            HASH_ITER(hh, online_members, curr, tmp) 
            {
                HASH_DEL(online_members, curr);
                free(curr);
            }
        }
        printf("%u users are currently online:\n", users_online);
    }

    //Extract each subsequent user's name
    while(token)
    {
        printf("%s\n", token);
        curr = malloc(sizeof(Member));
        strcpy(curr->username, token);
        HASH_ADD_STR(online_members, username, curr);
        
        token = strtok(NULL, ",");
    }
}


static void parse_control_message(char* cmd_buffer)
{
    char *old_buffer = buffer;
    buffer = cmd_buffer;
    
    if(strncmp("!useroffline=", buffer, 13) == 0)
        user_offline();

    else if(strncmp("!useronline=", buffer, 12) == 0)
        user_online();

    else if(strncmp("!longmsg=", buffer, 9) == 0)
        recv_long_msg();

    else if(strncmp("!userlist=", buffer, 10) == 0)
        parse_userlist();

    else if(strncmp("!groupinvite=", buffer, 13) == 0)
        group_invited();

    else if(strncmp("!groupjoined=", buffer, 13) == 0)
        group_joined();

    else if(strncmp("!leftgroup=", buffer, 11) == 0)
        user_left_group();

    else if(strncmp("!joinedgroup=", buffer, 13) == 0)
        user_joined_group();

    else if(strncmp("!groupkicked=", buffer, 13) == 0)
        group_kicked();

    else
        printf("Received invalid control message \"%s\"\n", buffer);

    buffer = old_buffer;
}


/******************************/
/*     Client Entry Point     */
/******************************/

static inline void client_main_loop()
{
     struct epoll_event events[MAX_EPOLL_EVENTS];
     int ready_count, i;
     int bytes;
     
     //Send messages to the host forever
    while(1)
    {
        
        //Wait until epoll has detected some event in the registered fd's
        ready_count = epoll_wait(epoll_fd, events, MAX_EPOLL_EVENTS, -1);
        if(ready_count < 0)
        {
            perror("epoll_wait failed!");
            return;
        }

        for(i=0; i<ready_count; i++)
        {
            //When the user enters a message from stdin
            if(events[i].data.fd == 0)
            {
                //Read from stdin and remove the newline character. TODO: Use readline() maybe?
                bytes = getline(&buffer, &buffer_size, stdin);
                remove_newline(buffer);
                --bytes;

                //Do not transmit empty messages
                if(strlen(buffer) < 1 || buffer[0] == '\n')
                    continue;

                //Transmit the line read from stdin to the server
                if(!send_msg(my_socketfd, buffer, strlen(buffer)+1))
                    return;

                //If the client has specified a command, check if anything needs to be done on the client side immediately
                if(buffer[0] == '!')
                {
                    if(handle_user_command() < 0)
                        exit(0);
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

    if(!name_is_valid(username))
        return;
    
    buffer = calloc(BUFSIZE, sizeof(char));
    userName = username;
    printf("Username: %s\n", username);


    /*Setup epoll to allow multiplexed IO to serve multiple clients*/
    epoll_fd = epoll_create1(0);
    if(epoll_fd < 0)
    {
        perror("Failed to create epoll!");
        return;
    }

    //Create a TCP socket 
    my_socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if(my_socketfd < 0)
    {
        perror("Error creating socket!");
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
        perror("Error connecting to the server!");
        return;
    }

    //Register with the server
    if(!register_with_server())
        return;

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