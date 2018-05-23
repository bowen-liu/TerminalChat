#include "file_transfer_server.h"
#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/timerfd.h>


#define XFER_SENDER_EPOLL_EVENTS    (EPOLLRDHUP | EPOLLIN | EPOLLONESHOT)
#define XFER_RECVER_EPOLL_EVENTS    (EPOLLRDHUP | EPOLLOUT | EPOLLONESHOT)


/******************************/
/*     Helpers and Shared     */
/******************************/ 

//Defined in library/crc32/crc32.c
extern unsigned int xcrc32 (const unsigned char *buf, int len, unsigned int init);

void print_server_xferargs(FileXferArgs_Server *args)
{    
    printf("Me: \"%s\" (fd=%d), Target: \"%s\", OP: %s, Filename: \"%s\", Filesize: %zu, Checksum: %x, Transferred: %zu, Token: %s\n", 
            args->myself->username, args->xfer_socketfd, args->target->username, (args->operation == SENDING_OP)? "Send":"Recv", args->filename, args->filesize, args->checksum, args->transferred, args->token);
}

void generate_token(char* dest, size_t bytes)
{
    const unsigned int smallest_val = '0', largest_val = 'z';
    unsigned int val, count = 0;
    time_t t;
    
    srand((unsigned) time(&t));

    while(count < bytes)
    {
        val = rand() % (largest_val + 1 - smallest_val) + smallest_val;

        //Only allow 0-9, A-Z, a-z
        if( (val >= '0' && val <= '9') || (val >= 'A' && val <= 'Z') || (val >= 'a' && val <= 'z') )
            dest[count++] = val;             
    }
}


int validate_transfer_target (FileXferArgs_Server *request, char* request_username, char* target_username, User** request_user_ptr, User** target_user_ptr)
{
    FileXferArgs_Server *xferargs;
    User* request_user, *target_user;
    enum sendrecv_op expected_target_op;
    int matches = 0;

    /*******************************************************************/
    /* Validates the request against the requester's own transfer args */
    /*******************************************************************/

    //Skip this step if request_username is NULL.
    if(request_username)
    {
        HASH_FIND_STR(active_users, request_username, request_user);
        if(!request_user)
        {
            printf("User \"%s\" not found\n", request_username);
            return 0;
        }

        //Match the information in the target user's transfer args with this request
        xferargs = request_user->c->file_transfers;
        if(xferargs && xferargs->operation == request->operation)
            if(strcmp(xferargs->token, request->token) == 0)
                if(strcmp(xferargs->filename, request->filename) == 0)
                    if(xferargs->filesize == request->filesize)
                        if(xferargs->checksum == request->checksum)
                            if(xferargs->transferred == 0)
                                matches = 1;

        if(!matches)
        {
            printf("No matching request found with requester \"%s\".\n", request_username);
            return 0;
        }
    }
    else
    {
        //Use current_client if request_username is not not specified 
        request_username = current_client->username;
        request_user = get_current_client_user();  
    }
         



    /************************************************************/
    /* Validates the request against the target's transfer args */
    /************************************************************/

    //Locate the target user
    HASH_FIND_STR(active_users, target_username, target_user);
    if(!target_user)
    {
        printf("User \"%s\" not found\n", target_username);
        return 0;
    }

    //Match the information in the target user's transfer args with this request
    xferargs = target_user->c->file_transfers;
    expected_target_op = (request->operation == SENDING_OP)? RECVING_OP:SENDING_OP;
    matches = 0;

    if(xferargs && xferargs->operation == expected_target_op)
        if(strcmp(xferargs->token, request->token) == 0)
            if(strcmp(xferargs->target->username, request_username) == 0)
                if(strcmp(xferargs->filename, request->filename) == 0)
                    if(xferargs->filesize == request->filesize)
                        if(xferargs->checksum == request->checksum)
                            if(xferargs->transferred == 0)
                                matches = 1;

    if(!matches)
    {
        printf("No matching file found with sender \"%s\".\n", target_username);
        return 0;
    }

    //Return the pointer of my user descriptor, and the target's user descriptor
    if(request_user_ptr)
        *request_user_ptr = request_user;
    if(target_user_ptr)
        *target_user_ptr = target_user;

    return 1;
}



/******************************/
/*        Disconnection       */
/******************************/ 

//Client "c" MUST be a transfer connection!
void cleanup_transfer_connection(Client *c)
{
    FileXferArgs_Server *xferargs = c->file_transfers;
    FileXferArgs_Server *target_xferargs;

    User *myself;
    Client *target_xfer_connection;

    //Check if the transfer connection has been terminated already (usually when the user quits while transferring files)
    if(!xferargs)
    {
        kill_connection(c->socketfd);
        return;
    }

    printf("Closing transfer connection (%s) for \"%s\"...\n", 
            (xferargs->operation == SENDING_OP)? "SEND":"RECV", xferargs->myself->username);
    
    xferargs->myself->c->file_transfers = NULL;                 //Also remove the transfer args at my main connection          
    target_xferargs = xferargs->target->c->file_transfers;

    kill_connection(c->socketfd);
    
    if(xferargs->piece_buffer)
        free(xferargs->piece_buffer);
    
    free(xferargs);
    c->file_transfers = NULL;

    //Close the target's transfer connection if it's still open
    if(target_xferargs && fcntl(target_xferargs->xfer_socketfd, F_GETFD) != -1)
    {
        HASH_FIND_INT(active_connections, &target_xferargs->xfer_socketfd, target_xfer_connection);

        if(target_xfer_connection)
            cleanup_transfer_connection(target_xfer_connection);
        else
            printf("Did not find target transfer connection!\n");
    }
}

//Client "c" MUST be a USER connection!
void cancel_user_transfer(Client *c)
{
    Client *xfer_connection;

    if(!c->file_transfers)
        return;

    //If the client already has an ongoing file transfer in progress
    HASH_FIND_INT(active_connections, &c->file_transfers->xfer_socketfd, xfer_connection);
    if(xfer_connection)
    {
       printf("Disconnecting ongoing transfer connection for user %s.\n", c->username);
       cleanup_transfer_connection(xfer_connection);
       c->file_transfers = NULL;
       return;
    }

    //If the client only has pending transfer invites
    if(c->file_transfers->timeout)
        cleanup_timer_event(c->file_transfers->timeout);

    free(c->file_transfers);
    c->file_transfers = NULL;

}


int transfer_invite_expired(Client *c)
{
    if(!c->file_transfers)
        return 0;
    
    //Notify the sender of file expiry
    sprintf(buffer, "!rejectfile=%s,reason=%s", c->file_transfers->target->username, "Expired");
    send_msg(c, buffer, strlen(buffer)+1);

    //Notify the receiver of file expiry
    sprintf(buffer, "!cancelfile=%s,reason=%s", c->username, "Expired");
    send_msg(c->file_transfers->target->c, buffer, strlen(buffer)+1);

    free(c->file_transfers);
    c->file_transfers = NULL;
}


/******************************************/
/* New Incoming File Transfer Connections */
/******************************************/ 

int register_send_transfer_connection()
{
    char sender_name[USERNAME_LENG+1], recver_name[USERNAME_LENG+1];
    User *myself, *target;
    FileXferArgs_Server request_args, *xferargs;

    if(current_client->connection_type != UNREGISTERED_CONNECTION)
    {
        printf("Connection already registered. Type: %d.\n", current_client->connection_type);
        disconnect_client(current_client);
        return 0;
    }

    printf("Got a new transfer connection (SEND) request!\n");
    printf("\"%s\"\n", buffer);

    
    sscanf(buffer, "!xfersend=%[^,],size=%zu,crc=%x,sender=%[^,],recver=%[^,],token=%[^,]", 
            request_args.filename, &request_args.filesize, &request_args.checksum, sender_name, recver_name, request_args.token);
    request_args.operation = SENDING_OP;
    
    if(!validate_transfer_target(&request_args, sender_name, recver_name, &myself, &target))
    {
        printf("Mismatching SENDING transfer connection.\n");
        send_msg(current_client, "WrongInfo", 10);
        disconnect_client(current_client);
        return 0;
    }

    //Update my original FileXferArgs
    xferargs = myself->c->file_transfers;
    xferargs->myself = myself;
    xferargs->xfer_socketfd =  current_client->socketfd;
    xferargs->operation = SENDING_OP;
    xferargs->target = target;
    xferargs->piece_buffer = malloc(BUFSIZE);

    current_client->connection_type = TRANSFER_CONNECTION;
    current_client->file_transfers = xferargs;

    sprintf(buffer, "Accepted");
    send_msg(current_client, buffer, strlen(buffer)+1);
    printf("Accepted SENDING transfer connection for file \"%s\" (%zu bytes, token: %s, checksum: %x), from \"%s\" to \"%s\".\n",
            xferargs->filename, xferargs->filesize, xferargs->token, xferargs->checksum, sender_name, recver_name);
    
    update_epoll_events(connections_epollfd, current_client->socketfd, XFER_SENDER_EPOLL_EVENTS);
    return 0;
}

int register_recv_transfer_connection()
{
    char sender_name[USERNAME_LENG+1], recver_name[USERNAME_LENG+1];
    User *myself, *target;
    FileXferArgs_Server request_args, *xferargs;

    if(current_client->connection_type != UNREGISTERED_CONNECTION)
    {
        printf("Connection already registered. Type: %d.\n", current_client->connection_type);
        disconnect_client(current_client);
        return 0;
    }

    printf("Got a new transfer connection (RECV) request!\n");

    request_args.xfer_socketfd = current_client->socketfd;
    request_args.operation = RECVING_OP;

    sscanf(buffer, "!xferrecv=%[^,],size=%zu,crc=%x,sender=%[^,],recver=%[^,],token=%[^,]", 
            request_args.filename, &request_args.filesize, &request_args.checksum, sender_name, recver_name, request_args.token);

    if(!validate_transfer_target(&request_args, recver_name, sender_name, &myself, &target))
    {
        printf("Mismatching RECEIVING transfer connection.\n");
        send_msg(current_client, "WrongInfo", 10);
        disconnect_client(current_client);
        return 0;
    }

    //Update my original FileXferArgs
    xferargs = myself->c->file_transfers;
    xferargs->myself = myself;
    xferargs->xfer_socketfd =  current_client->socketfd;
    xferargs->operation = RECVING_OP;
    xferargs->target = target;

    current_client->connection_type = TRANSFER_CONNECTION;
    current_client->file_transfers = xferargs;

    sprintf(buffer, "Accepted");
    send_msg(current_client, buffer, strlen(buffer)+1);
    printf("Accepted RECEIVING transfer connection for file \"%s\" (%zu bytes, token: %s), from \"%s\" to \"%s\".\n", 
            xferargs->filename, xferargs->filesize, xferargs->token, sender_name, recver_name);

    update_epoll_events(connections_epollfd, current_client->socketfd, XFER_RECVER_EPOLL_EVENTS);
    return 0;
}



/******************************/
/* Client-Client File Sharing */
/******************************/ 

int new_client_transfer()
{
    FileXferArgs_Server *xferargs = calloc(1, sizeof(FileXferArgs_Server));
    char target_name[USERNAME_LENG+1];

    struct itimerspec timer_value;

    sscanf(buffer, "!sendfile=%[^,],size=%zu,crc=%x,target=%s", 
            xferargs->filename, &xferargs->filesize, &xferargs->checksum, target_name);

    //Find the target user specified
    HASH_FIND_STR(active_users, target_name, xferargs->target);
    if(!xferargs->target)
    {
        printf("User \"%s\" not found\n", target_name);
        free(xferargs);
        send_msg(current_client, "UserNotFound", 13);
        return 0;
    }

    current_client->file_transfers = xferargs;
    xferargs->operation = SENDING_OP;

    //Generate an unique token for this transfer
    generate_token(xferargs->token, TRANSFER_TOKEN_SIZE);

    //Send out a file transfer request to the target
    sprintf(buffer, "!sendfile=%s,size=%zu,crc=%x,target=%s,token=%s", 
            xferargs->filename, xferargs->filesize, xferargs->checksum, current_client->username, xferargs->token);
    printf("Forwarding file transfer request from user \"%s\" to  user \"%s\", for file \"%s\" (%zu bytes, token: %s, checksum: %x)\n", 
            current_client->username, xferargs->target->username, xferargs->filename, xferargs->filesize, xferargs->token, xferargs->checksum);

    send_msg(xferargs->target->c, buffer, strlen(buffer)+1);
    send_msg(current_client, "Delivered", 10);

    //Set a timeout event for the request
    xferargs->timeout = calloc(1, sizeof(TimerEvent));
    xferargs->timeout->event_type = EXPIRING_TRANSFER_REQ;
    xferargs->timeout->c = current_client;

    xferargs->timeout->timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if(xferargs->timeout->timerfd < 0)
    {
        perror("Failed to create a timerfd.");
        return 0;
    }

    memset(&timer_value, 0, sizeof(struct itimerspec));
    timer_value.it_value.tv_sec = XFER_REQUEST_TIMEOUT;

    if(timerfd_settime(xferargs->timeout->timerfd, 0, &timer_value, NULL) < 0)
    {
        perror("Failed to arm event timer.");
        return 0;
    }

    //Register the timer
    HASH_ADD_INT(timers, timerfd, xferargs->timeout);
    if(!register_fd_with_epoll(timers_epollfd, xferargs->timeout->timerfd, EPOLLIN | EPOLLONESHOT))
        return 0;   

    return 1;
}

int accepted_file_transfer()
{
    char target_username[USERNAME_LENG+1];
    User *target;
    FileXferArgs_Server *xferargs = calloc(1, sizeof(FileXferArgs_Server));

    sscanf(buffer, "!acceptfile=%[^,],size=%zu,crc=%x,target=%[^,],token=%s", 
            xferargs->filename, &xferargs->filesize, &xferargs->checksum, target_username, xferargs->token);
    printf("User \"%s\" has accepted the file \"%s\" (%zu bytes, token: %s, checksum: %x) from user \"%s\"\n", 
            current_client->username, xferargs->filename, xferargs->filesize, xferargs->token, xferargs->checksum, target_username);

    xferargs->operation = RECVING_OP;

    //Matches with the target user's transfer information
    if(!validate_transfer_target (xferargs, NULL, target_username, NULL, &target))
    {
        printf("Transfer information mismatched. Cancelling...\n");
        send_msg(current_client, "WrongInfo", 10);
        free(xferargs);
        return 0;
    }

    xferargs->target = target;
    current_client->file_transfers = xferargs;

    //Cancel the timeout timer on the sender side
    cleanup_timer_event(target->c->file_transfers->timeout);
    target->c->file_transfers->timeout = NULL;
    
    //Forward the accept message to the sender
    sprintf(buffer, "!acceptfile=%s,size=%zu,crc=%x,target=%s,token=%s", 
            xferargs->filename, xferargs->filesize, xferargs->checksum, current_client->username, xferargs->token);
    return send_msg(xferargs->target->c, buffer, strlen(buffer)+1);
}


int rejected_file_transfer()
{
    char target_name[USERNAME_LENG+1];
    char reason[MAX_MSG_LENG+1];
    User *target;

    sscanf(buffer, "!rejectfile=%[^,],reason=%s", target_name, reason);

    HASH_FIND_STR(active_users, target_name, target);
    if(!target->c->file_transfers || strcmp(target->c->file_transfers->target->username, current_client->username) != 0)
    {
        printf("User has no pending file transfer.\n");
        send_msg(current_client, "NoFileFound", 12); 
        return 0;
    }

    printf("File Transfer with \"%s\" has been cancelled. Reason: \"%s\"\n", target_name, reason);
    send_msg(current_client, "Cancelled", 10); 

    //Notify the target 
    sprintf(buffer, "!rejectfile=%s,reason=%s", current_client->username, reason);
    send_msg(target->c, buffer, strlen(buffer)+1);
    cancel_user_transfer(target->c);

    return 1;
}


int user_cancelled_transfer()
{
    char target_name[USERNAME_LENG+1];
    char reason[MAX_MSG_LENG+1];
    
    sscanf(buffer, "!cancelfile=%[^,],reason=%s", target_name, reason);

    if(!current_client->file_transfers)
    {
        printf("User has no pending or ongoing file transfer to cancel.\n");
        send_msg(current_client, "NoFileFound", 12); 
        return 0;
    }
    printf("File Transfer with \"%s\" has been cancelled. Reason: \"%s\"\n", target_name, reason);
    send_msg(current_client, "Cancelled", 10); 

    //Notify the target 
    sprintf(buffer, "!cancelfile=%s,reason=%s", current_client->username, reason);
    send_msg(current_client->file_transfers->target->c, buffer, strlen(buffer)+1);    
    cancel_user_transfer(current_client);

    return 1;
}


/* FORWARDING FILE PIECES */

//The receiver is ready for receiving the next piece (EPOLLOUT received)
int client_data_forward_recver_ready()
{
    FileXferArgs_Server *xferargs = current_client->file_transfers;
    FileXferArgs_Server *sender_xferargs = current_client->file_transfers->target->c->file_transfers;

    char *myname = xferargs->myself->username;
    int bytes, bytes_sent;


    //Nothing to send if the last piece has been completely forwarded
    if(sender_xferargs->piece_size == 0)
    {
        //Rearm epoll notifications for both sender and receiver for the next piece
        update_epoll_events(connections_epollfd, sender_xferargs->xfer_socketfd, XFER_SENDER_EPOLL_EVENTS);
        update_epoll_events(connections_epollfd, current_client->socketfd, XFER_RECVER_EPOLL_EVENTS);
        return 0;
    }
       

    //printf("Piece size: %zu, piece transferred %zu\n", sender_xferargs->piece_size, sender_xferargs->piece_transferred);
    bytes = sender_xferargs->piece_size - sender_xferargs->piece_transferred;
    if(bytes <= 0)
    {
        sender_xferargs->piece_size = 0;
        sender_xferargs->piece_transferred = 0;
        return 0;
    }

    bytes_sent = send_msg_direct(current_client->socketfd, &sender_xferargs->piece_buffer[sender_xferargs->piece_transferred], bytes);
    //bytes_sent = send_msg_direct(current_client->socketfd, &sender_xferargs->piece_buffer[sender_xferargs->piece_transferred], (LONG_RECV_PAGE_SIZE > bytes)? bytes:LONG_RECV_PAGE_SIZE);
    if(bytes_sent < 0)
    {
        perror("Failed to send the current piece");
        return -1;
    }

    xferargs->transferred += bytes_sent;
    sender_xferargs->transferred += bytes_sent;
    sender_xferargs->piece_transferred += bytes_sent;

    //Were we able to forward the entire received piece?
    if(sender_xferargs->piece_transferred >= sender_xferargs->piece_size)
    {
        sender_xferargs->piece_size = 0;
        sender_xferargs->piece_transferred = 0;

        //Rearm epoll notifications for sender (to send the next piece)
        update_epoll_events(connections_epollfd, sender_xferargs->xfer_socketfd, XFER_SENDER_EPOLL_EVENTS);
    }

    printf("Forwarded %d bytes (%zu\\%zu) from \"%s\" to \"%s\".\n", 
            bytes_sent, xferargs->transferred, xferargs->filesize, myname, xferargs->target->username);

    if(xferargs->transferred == xferargs->filesize)
        printf("All bytes for file transfer has been forwarded. Waiting for receiver \"%s\" to close the connection...\n", xferargs->target->username);
    else
        //Rearm epoll notifications for receiver (to receive the next chunk/piece)
        update_epoll_events(connections_epollfd, current_client->socketfd, XFER_RECVER_EPOLL_EVENTS);

    return bytes_sent;
}


//The sender has a new piece ready (EPOLLIN received)
int client_data_forward_sender_ready()
{
    FileXferArgs_Server *xferargs = current_client->file_transfers;
    FileXferArgs_Server *recver_xferargs = current_client->file_transfers->target->c->file_transfers;

    char *myname = xferargs->myself->username;
    int bytes, bytes_sent;
    
    //Do not receive a new piece from the sender if the last piece hasn't been fully forwarded yet
    if(xferargs->piece_size > 0)
        return 0;

    //Receive a new piece of data that was sent by the sender, if the old piece has been completely forwarded already
    bytes = recv_msg(current_client, xferargs->piece_buffer, BUFSIZE);
    if(!bytes)
        return 0;

    xferargs->piece_size = bytes;
    bytes_sent = send_msg_direct(recver_xferargs->xfer_socketfd, xferargs->piece_buffer, bytes);
    //bytes_sent = send_msg_direct(recver_xferargs->xfer_socketfd, xferargs->piece_buffer, (LONG_RECV_PAGE_SIZE > bytes)? bytes:LONG_RECV_PAGE_SIZE);
    if(bytes_sent < 0)
    {
        perror("Failed to send the current piece");
        return -1;
    }    

    recver_xferargs->transferred += bytes_sent;
    xferargs->transferred += bytes_sent;
    xferargs->piece_transferred += bytes_sent;

    //Were we able to forward the entire received piece?
    if(xferargs->piece_transferred >= xferargs->piece_size)
    {
        xferargs->piece_size = 0;
        xferargs->piece_transferred = 0;
    }

    printf("Forwarded %d bytes (%zu\\%zu) from \"%s\" to \"%s\".\n", 
            bytes_sent, xferargs->transferred, xferargs->filesize, myname, xferargs->target->username);

    if(xferargs->transferred == xferargs->filesize)
        printf("All bytes for file transfer has been forwarded. Waiting for receiver \"%s\" to close the connection...\n", xferargs->target->username);
    else
        //Rearm epoll notifications for receiver (to receive the next chunk/piece)
        update_epoll_events(connections_epollfd, recver_xferargs->xfer_socketfd, XFER_RECVER_EPOLL_EVENTS);

    return bytes_sent;
}

