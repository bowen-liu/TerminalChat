#include "client.h"

#include <pthread.h>
#include <readline/readline.h>      //sudo apt-get install libreadline-dev 
#include <readline/history.h>


char* my_username;

//Socket for communicating with the server
int my_socketfd;                                    //My (client) main socket connection with the server
struct sockaddr_in server_addr;                     //Server's information struct
int epoll_fd;

pthread_mutex_t buffer_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_t connection_thread;

//Receive buffers
char *buffer;
char *msg_target;
char *msg_body;
static size_t last_received;

//Used if the current message is still being sent or received
Pending_Msg pending_msg;



static void exit_cleanup()
{
    if(file_transfers)
        cancel_transfer(file_transfers);
    
    close(my_socketfd);
    pthread_cancel(connection_thread);
}

/******************************/
/*     Basic Send/Receive     */
/******************************/

static inline unsigned int transfer_next_client()
{
    int retval;

    retval = transfer_next_common(my_socketfd, &pending_msg);
    if(retval <= 0)
        exit(0);
    
    //Remove the EPOLLOUT notification once long send has completed
    if(pending_msg.pending_op == NO_XFER_OP)
    {
        update_epoll_events(epoll_fd, my_socketfd, CLIENT_EPOLL_FLAGS);
        printf("Completed partial transfer.\n");
    }
    
    return retval;
}

inline int send_msg_client(char* buffer, size_t size)
{
    int retval = send_msg_common(my_socketfd, buffer, size, &pending_msg);

    if(retval < 0)
        exit(retval);

    if(pending_msg.pending_op == SENDING_OP)
        update_epoll_events(epoll_fd, my_socketfd, CLIENT_EPOLL_FLAGS | EPOLLOUT);

    return retval;
}

inline int recv_msg_client(char* buffer, size_t size)
{
    int retval = recv_msg_common(my_socketfd, buffer, size, &pending_msg);
    
    if(retval < 0)
        exit(retval);

    return retval;
}


/******************************/
/*   Client-side Operations   */
/******************************/

static void parse_userlist();
static int register_with_server()
{   
    /*This function is called right after a connection to the server is made, and before the connection is registered to epoll. 
    Therefore, all send/recv here are still blocking, and thus all actions done here are synchronous. 
    Additionally, the client/server will not deal with partial send/recvs before registration is complete*/

    //Receive a greeting message from the server upon connecting to the server
    if(recv_direct(my_socketfd, buffer, BUFSIZE) <= 0)
        return 0;
    printf("%s\n", buffer);

    //Register my desired username
    printf("Registering username \"%s\"...\n", my_username);
    sprintf(buffer, "!register:username=%s", my_username);
    if(send_direct(my_socketfd, buffer, strlen(buffer)+1) <= 0)
        return 0;

    //Parse registration reply from server
    if(recv_direct(my_socketfd, buffer, BUFSIZE) <= 0)
        return 0;

    //Did we receive an anticipated !regreply?
    if(strncmp(buffer, "!regreply:username=", 19) == 0)
    {
        sscanf(&buffer[19], "%s", my_username);
        printf("Registered with server as \"%s\"\n", my_username);
    }
    else
        goto register_with_server_failed;



    /************************************/
    /*     Join the main chat lobby     */
    /************************************/

    //Request a list of active users from the server
    sprintf(buffer, "!join @@%s", LOBBY_GROUP_NAME);
    if(send_msg_client(buffer, strlen(buffer)+1) <= 0)
        return 0;
    
    //Wait for server to reply
    if(recv_msg_client(buffer, BUFSIZE) <= 0)
        return 0;
    
    //Parse the returned userlist
    if(strncmp(buffer, "!joined=", 8) == 0)
        user_joined_group();
    else
        goto register_with_server_failed;

    
    /************************************/
    /* Get the current active user list */
    /************************************/

    //Request a list of active users from the server
    strcpy(buffer, "!userlist");
    if(send_msg_client(buffer, strlen(buffer)+1) < 0)
        return 0;

    //The returned userlist will be interpreted later in the client main loop  
    return 1;


register_with_server_failed:

    //Something went wrong and server doesn't want me to join :(
    printf("Server rejected registration: \"%s\"\n", buffer);
    return 0;
}



static int handle_user_command()
{
    //Return 0 if you don't want the command to be forwarded

    if(strcmp(msg_body, "!close") == 0)
    {
        printf("Closing connection!\n");
        exit(0);
    }

    /*Group operations. Implemented in group.c*/

    else if(strncmp(msg_body, "!leave ", 12) == 0)
        return leaving_group();

    /*File Transfer operations. Implemented in file_transfer_client.c*/

    else if(strncmp("!sendfile ", msg_body, 10) == 0)
        return outgoing_file();
    
    else if(strcmp("!acceptfile", msg_body) == 0)
        return accept_incoming_file();

    else if(strcmp("!rejectfile", msg_body) == 0)
        return reject_incoming_file();

    else if(strcmp("!cancelfile", msg_body) == 0)
        return cancel_ongoing_file_transfer();

    else if(strncmp("!putfile ", msg_body, 9) == 0)   
        return outgoing_file_group();

    return 1;
}



/******************************/
/*  Server Control Messages   */
/******************************/

static void parse_userlist()
{
    char group_name[USERNAME_LENG+1];
    char* newbuffer = buffer;
    char* token;
    unsigned int users_online;

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
        printf("%u user(s) are currently online in the group \"%s\":\n", users_online, group_name);
        token = strtok(NULL, ",");
    }

    //This is a global userlist
    else
        printf("%u users are currently online:\n", users_online);

    //Extract each subsequent user's name
    while(token)
    {
        printf("%s\n", token);
        token = strtok(NULL, ",");
    }
}


static void parse_control_message(char* cmd_buffer)
{
    char *old_buffer = buffer;
    buffer = cmd_buffer;
    
    if(strncmp("!userlist=", buffer, 10) == 0)
        parse_userlist();

    /*Group operations. Implemented in group.c*/

    else if(strncmp("!invite=", buffer, 8) == 0)
        group_invited();

    else if(strncmp("!left=", buffer, 6) == 0)
        user_left_group();

    else if(strncmp("!joined=", buffer, 8) == 0)
        user_joined_group();

    else if(strncmp("!kicked=", buffer, 8) == 0)
        group_kicked();

    else if(strncmp("!banned=", buffer, 8) == 0)
        group_banned();

    /*File Transfer operations. Implemented in file_transfer_client.c*/

    else if(strncmp("!sendfile=", buffer, 10) == 0)
        incoming_file();

    else if(strncmp("!acceptfile=", buffer, 12) == 0)
        recver_accepted_file();

    else if(strncmp("!rejectfile=", buffer, 12) == 0)
        rejected_file_sending();
    
    else if(strncmp("!cancelfile=", buffer, 12) == 0)
        file_transfer_cancelled();

    else if(strncmp("!filelist=", buffer, 10) == 0)
        parse_filelist();

    else if(strncmp("!putfile=", buffer, 9) == 0)
        new_group_file_ready();

    else if(strncmp("!getfile=", buffer, 9) == 0)
        incoming_group_file();
    


    else
        printf("Received invalid control message \"%s\"\n", buffer);

    buffer = old_buffer;
}



/******************************/
/*   Core Client Operations   */
/******************************/

static void handle_main_socket_events(int events)
{         
    char *old_buffer = NULL;
    
    /*Server has dropped our connection*/
    if(events & EPOLLRDHUP)
    {
        printf("Connection with the server has been closed.\n");
        exit(0);
    }


    /*Server has sent us new message to read*/
    else if(events & EPOLLIN)
    {
        //Return to receiving a long message if it's still pending
        if(pending_msg.pending_op == RECVING_OP)
        {
            transfer_next_client();

            if(pending_msg.pending_op == NO_XFER_OP)
            {
                printf("Done Receiving.\n");
                old_buffer = buffer;
                buffer = pending_msg.pending_buffer;
            }
            else
                return;
        }

        //Otherwise, receive a regular new message from the server
        else
        {
            last_received = recv_msg_client(buffer, BUFSIZE);
            if(last_received <= 0)
                exit(0);

            //Did not finish receiving the incoming message
            if(pending_msg.pending_op != NO_XFER_OP)
                return;
        }
        
        //Process the received message from server
        if(buffer[0] == '!')
            parse_control_message(buffer);
        else
            printf("%s\n", buffer);


        //Cleanup partial recv args, if used
        if(old_buffer)
        {
            buffer = old_buffer;
            old_buffer = NULL;
            clean_pending_msg(&pending_msg);
        }
    }


    /*Server/socket is ready to continue receive partial sends from us*/
    else if(events & EPOLLOUT)
    {
        //Return to receiving a long message if it's still pending
        if(pending_msg.pending_op == SENDING_OP)
        {
            transfer_next_client();

            //Completed sending all pieces
            if(pending_msg.pending_op == NO_XFER_OP)
                clean_pending_msg(&pending_msg);
        }
    }
}


static void handle_transfer_connection_events(int events)
{
    if(!file_transfers)
        printf("No pending file transfers. Ignoring event...\n");

    //The transfer connection has been terminated by the server (upon completion or failure)
    if(events & EPOLLRDHUP)
    {
        printf("Server has closed our transfer connection.\n");
        
        if(file_transfers->transferred != file_transfers->filesize)
            printf("Transferred %zu\\%zu bytes with \"%s\" before failure.\n", 
                    file_transfers->transferred, file_transfers->filesize, file_transfers->target_name);

        cancel_transfer(file_transfers);
    }   

    //The transfer connection is ready for receiving
    else if(events & EPOLLIN)
        file_recv_next(file_transfers);
    
    //The transfer connection is ready for sending
    else if(events & EPOLLOUT)
        file_send_next(file_transfers);
}


static inline void client_main_loop()
{
     struct epoll_event events[MAX_EPOLL_EVENTS];
     int ready_count, i;
     
    while(1)
    {
        //Wait until epoll has detected some event in the registered fd's
        ready_count = epoll_wait(epoll_fd, events, MAX_EPOLL_EVENTS, -1);
        if(ready_count < 0)
        {
            perror("epoll_wait failed!");
            exit(0);
        }

        pthread_mutex_lock(&buffer_lock);

        for(i=0; i<ready_count; i++)
        {
            /* Message from Server */
            if(events[i].data.fd == my_socketfd)
                handle_main_socket_events(events[i].events);
  
            /* Transfer connections (and related fd's) */
            else if (file_transfers)
            {   
                if(events[i].data.fd == file_transfers->socketfd)
                    handle_transfer_connection_events(events[i].events);

                else if(events[i].data.fd == file_transfers->timerfd)
                    print_transfer_progress();
            }
        }

        pthread_mutex_unlock(&buffer_lock);
    }

}


void handle_client_input()
{
    char *str = NULL;
    char *prompt = NULL;
    
    while(1)
    {
        str = readline(prompt);
        pthread_mutex_lock(&buffer_lock);
       
        //Do not transmit empty messages
        if(!str)
            goto handle_client_input_cleanup;

        if(strlen(str) < 1 || str[0] == '\n')
             goto handle_client_input_cleanup;

        strcpy(buffer, str);
        seperate_target_command(buffer, &msg_target, &msg_body);

        //If the client has specified a command, check if anything needs to be done on the client side immediately
        if(msg_body && msg_body[0] == '!')
        {
            if(!handle_user_command())
                 goto handle_client_input_cleanup;
        }

        //Restore the space between the target and message body, if it was removed by seperate_target_command() and hasn't been altered
        if(msg_target && msg_body && *(msg_body-1) == '\0')
            *(msg_body-1) = ' ';

        //Transmit the line read from stdin to the server
        send_msg_client(buffer, strlen(buffer)+1);

handle_client_input_cleanup:

        if(str)
        {
            free(str);
            str = NULL;
        }
        pthread_mutex_unlock(&buffer_lock);
    }
}



void client(const char* hostname, const unsigned int port,  char *username)
{
    char ipaddr[INET_ADDRSTRLEN], port_str[8];

    atexit(exit_cleanup);

    if(!name_is_valid(username))
        return;

    my_username = username;
    printf("Running as client with username: %s...\n", username);

    buffer = calloc(BUFSIZE, sizeof(char));
    memset(&pending_msg, 0 ,sizeof(Pending_Msg));

    /*Setup epoll to allow multiplexed IO to serve multiple clients*/
    epoll_fd = epoll_create1(0);
    if(epoll_fd < 0)
    {
        perror("Failed to create epoll!");
        return;
    }

    //Resolve the server's hostname
    sprintf(port_str, "%u", port);
    if(!hostname_to_ip(hostname, port_str, ipaddr))
        return;

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
    printf("Connecting to server at %s:%u... \n", ipaddr, ntohs(server_addr.sin_port));
    if(connect(my_socketfd, (struct sockaddr*) &server_addr, sizeof(struct sockaddr)) < 0)
    {
        perror("Failed to connect to server.");
        return;
    }
    printf("Connected!\n");
   

    //Register with the server
    if(!register_with_server())
        return;

    /*Register the server socket to the epoll list, and also mark it as nonblocking*/
    fcntl(my_socketfd, F_SETFL, O_NONBLOCK);
    if(!register_fd_with_epoll(epoll_fd, my_socketfd, CLIENT_EPOLL_FLAGS))
        return;   

    /*Spawn the main network/event loop as a seperate thread*/
    if(pthread_create(&connection_thread, NULL, (void*) &client_main_loop, NULL) != 0)
    {
        printf("Failed to create main connection thread\n");
        return;
    }

    handle_client_input();
}
