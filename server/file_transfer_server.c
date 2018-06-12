#include "file_transfer_server.h"
#include "server.h"

#include <time.h>
#include <sys/timerfd.h>
#include <sys/stat.h>
#include <unistd.h>


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
            args->myself->username, args->xfer_socketfd, args->target_user->username, (args->operation == SENDING_OP)? "Send":"Recv", args->filename, args->filesize, args->checksum, args->transferred, args->token);
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


static int validate_transfer_user (FileXferArgs_Server *request, char* username, char* target_username, XferTarget* requester_ret)
{
    FileXferArgs_Server *xferargs;
    User *request_user;
    Group *request_group;

    int matches = 0;

    /*******************************************************************/
    /* Validates the request against the requester's own transfer args */
    /*******************************************************************/

    HASH_FIND_STR(active_users, username, request_user);
    if(!request_user)
        HASH_FIND_STR(groups, username, request_group);

    if(request_user)
    {            
        //If not null, return the found requester
        if(requester_ret)
        {
            requester_ret->target_type = USER_TARGET;
            requester_ret->user = request_user;
        }   

        //Match the information in the target user's transfer args with this request
        xferargs = request_user->c->file_transfers;
        if(xferargs && xferargs->operation == request->operation)
            if(strcmp(xferargs->token, request->token) == 0)
                if(strcmp(xferargs->target_user->username, target_username) == 0)
                    if(strcmp(xferargs->filename, request->filename) == 0)
                        if(xferargs->filesize == request->filesize)
                            if(xferargs->checksum == request->checksum)
                                if(xferargs->transferred == 0)
                                    matches = 1;
    }
    else if(request_group)
    {
        //If not null, return the found requester
        if(requester_ret)
        {
            requester_ret->target_type = GROUP_TARGET;
            requester_ret->group = request_group;
        }  
        
        matches = 1;
    }
    else
    {
        printf("Transfer source \"%s\" not found\n", username);
        return 0;
    }


    if(!matches)
        printf("No matching request found with requester \"%s\".\n", username);
    
    return matches;
}

static inline int validate_transfer_target(FileXferArgs_Server *request, char* request_username, char* target_username, XferTarget* target_ret)
{
    enum sendrecv_op orig_operation = request->operation;
    int retval;

    request->operation = (request->operation == SENDING_OP)? RECVING_OP:SENDING_OP;
    retval = validate_transfer_user(request, target_username, request_username, target_ret);
    request->operation = orig_operation;

    return retval;
}


static int validate_transfer(FileXferArgs_Server *request, char* request_username, char* target_username, XferTarget* requester_ret, XferTarget* target_ret)
{
    int retval;
    
    /*Validate against transfer requester*/
    retval = validate_transfer_user(request, request_username, target_username, requester_ret);
    if(!retval)
        return 0;

    /*Validate against transfer target*/
    retval = validate_transfer_target(request, request_username, target_username, target_ret);
    if(!retval)
        return 0;
    
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

    Client *target_xfer_connection;

    if(c->connection_type != TRANSFER_CONNECTION)
    {
        printf("Trying to kill a non transfer connection\n");
        return;
    }

    //Check if the transfer connection has been terminated already (usually when the user quits while transferring files)
    if(!xferargs)
    {
        kill_connection(c->socketfd);
        return;
    }

    printf("Closing transfer connection (%s) for \"%s\"...\n", 
            (xferargs->operation == SENDING_OP)? "SEND":"RECV", xferargs->myself->username);
    
    //Also remove the transfer args at my main connection, if this is a client-client file transfer
    xferargs->myself->c->file_transfers = NULL;                 
    
    kill_connection(c->socketfd);
    
    if(xferargs->piece_buffer)
        free(xferargs->piece_buffer);

    if(xferargs->file_fp)
        fclose(xferargs->file_fp);
    
    //Cleanup for group transfers
    if(xferargs->target_type == GROUP_TARGET)
    {
        //Delete the local file if the previous PUT operation failed
        if(xferargs->operation == SENDING_OP && xferargs->transferred < xferargs->filesize)
        {
            if(remove(xferargs->target_file) < 0)
                perror("Failed to delete file.");
        }

        free(xferargs);
        c->file_transfers = NULL;
        return;
    }


    /*Cleanup for client-client transfers*/

    target_xferargs = xferargs->target_user->c->file_transfers;
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
    {
        printf("User %s have no pending transfers to cancel.\n", c->username);
        return;
    }

    //If the client already has an ongoing file transfer in progress, close the connection
    if(c->file_transfers->xfer_socketfd)
    {
        HASH_FIND_INT(active_connections, &c->file_transfers->xfer_socketfd, xfer_connection);
        if(xfer_connection)
        {
            printf("Disconnecting ongoing transfer connection for user %s.\n", c->username);
            cleanup_transfer_connection(xfer_connection);
            c->file_transfers = NULL;
            return;
        }

        printf("Cannot find user \"%s\"'s transfer socket.\n ", c->username);
    }


    //If the client only has pending transfer invites
    if(c->file_transfers->timeout)
        cleanup_timer_event(c->file_transfers->timeout);
        

    free(c->file_transfers);
    c->file_transfers = NULL;
}


void transfer_invite_expired(Client *c)
{
    if(!c->file_transfers)
        return;
    
    //Notify the sender of file expiry
    sprintf(buffer, "!rejectfile=%s,reason=%s", c->file_transfers->target_user->username, "Expired");
    send_msg(c, buffer, strlen(buffer)+1);

    //Notify the receiver of file expiry
    sprintf(buffer, "!cancelfile=%s,reason=%s", c->username, "Expired");
    send_msg(c->file_transfers->target_user->c, buffer, strlen(buffer)+1);

    cancel_user_transfer(c);
}



/******************************************/
/* New Incoming File Transfer Connections */
/******************************************/ 

//Note: Both functions does not handle partial sends/recvs, as it deals solely with unconnected clients

int register_send_transfer_connection()
{
    char sender_name[USERNAME_LENG+1], recver_name[USERNAME_LENG+1];
    XferTarget myself_ret, target_ret;
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

    //Validate if the registration information matches the one the requester and target's user info
    if(!validate_transfer(&request_args, sender_name, recver_name, &myself_ret, &target_ret))
    {
        printf("Mismatching SENDING transfer connection.\n");
        send_direct(current_client->socketfd, "WrongInfo", 10);
        disconnect_client(current_client);
        return 0;
    }

    
    if(myself_ret.target_type != USER_TARGET)
    {
        printf("Sender is not a user!\n");
        send_direct(current_client->socketfd, "WrongInfo", 10);
        disconnect_client(current_client);
        return 0;
    }

    //Update my original FileXferArgs
    xferargs =  myself_ret.user->c->file_transfers;
    xferargs->myself =  myself_ret.user;
    xferargs->xfer_socketfd =  current_client->socketfd;
    xferargs->operation = SENDING_OP;
    xferargs->piece_buffer = malloc(XFER_BUFSIZE);

    if(target_ret.target_type == USER_TARGET)
    {
        xferargs->target_user = target_ret.user;
        xferargs->target_type = USER_TARGET;
    }
    else if(target_ret.target_type == GROUP_TARGET)
    {
        xferargs->target_group = target_ret.group;
        xferargs->target_type = GROUP_TARGET;
    }
    else
    {
        printf("Target is not a user/group!\n");
        send_direct(current_client->socketfd, "WrongInfo", 10);
        disconnect_client(current_client);
        return 0;
    }

    current_client->connection_type = TRANSFER_CONNECTION;
    current_client->file_transfers = xferargs;

    sprintf(buffer, "Accepted");
    send_direct(current_client->socketfd, buffer, strlen(buffer)+1);
    printf("Accepted SENDING transfer connection for file \"%s\" (%zu bytes, token: %s, checksum: %x), from \"%s\" to \"%s\".\n",
            xferargs->filename, xferargs->filesize, xferargs->token, xferargs->checksum, sender_name, recver_name);

    update_epoll_events(connections_epollfd, current_client->socketfd, XFER_SENDER_EPOLL_EVENTS);

    //Cancel the idle timer
    cleanup_timer_event(current_client->idle_timer);
    current_client->idle_timer = NULL;

    return 0;
}

int register_recv_transfer_connection()
{
    char sender_name[USERNAME_LENG+1], recver_name[USERNAME_LENG+1];
    XferTarget myself_ret, target_ret;
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

    //Validate if the registration information matches the one the requester and target's user info
    if(!validate_transfer(&request_args, recver_name, sender_name, &myself_ret, &target_ret))
    {
        printf("Mismatching RECEIVING transfer connection.\n");
        send_direct(current_client->socketfd, "WrongInfo", 10);
        disconnect_client(current_client);
        return 0;
    }


    //Update my original FileXferArgs
    xferargs =  myself_ret.user->c->file_transfers;
    xferargs->myself =  myself_ret.user;
    xferargs->xfer_socketfd =  current_client->socketfd;
    xferargs->operation = RECVING_OP;

    if(target_ret.target_type == USER_TARGET)
    {
        xferargs->target_user = target_ret.user;
        xferargs->target_type = USER_TARGET;
    }
    else if(target_ret.target_type == GROUP_TARGET)
    {
        xferargs->target_group = target_ret.group;
        xferargs->target_type = GROUP_TARGET;
    }
    else
    {
        printf("Target is not a user/group!\n");
        send_direct(current_client->socketfd, "WrongInfo", 10);
        disconnect_client(current_client);
        return 0;
    }

    current_client->connection_type = TRANSFER_CONNECTION;
    current_client->file_transfers = xferargs;

    sprintf(buffer, "Accepted");
    send_direct(current_client->socketfd, buffer, strlen(buffer)+1);
    printf("Accepted RECEIVING transfer connection for file \"%s\" (%zu bytes, token: %s), from \"%s\" to \"%s\".\n", 
            xferargs->filename, xferargs->filesize, xferargs->token, sender_name, recver_name);

    update_epoll_events(connections_epollfd, current_client->socketfd, XFER_RECVER_EPOLL_EVENTS);

    //Cancel the idle timer
    cleanup_timer_event(current_client->idle_timer);
    current_client->idle_timer = NULL;

    return 0;
}



/******************************/
/* Client-Client File Sharing */
/******************************/ 

int new_client_transfer()
{
    FileXferArgs_Server *xferargs = calloc(1, sizeof(FileXferArgs_Server));
    char target_name[USERNAME_LENG+1];

    if(!msg_target)
        return 0;
    ++msg_target;

    sscanf(msg_body, "!sendfile=%[^,],size=%zu,crc=%x", 
            xferargs->filename, &xferargs->filesize, &xferargs->checksum);
    strcpy(target_name, msg_target);

    //Find the target user specified
    HASH_FIND_STR(active_users, msg_target, xferargs->target_user);
    if(!xferargs->target_user)
    {
        printf("User \"%s\" not found\n", msg_target);
        free(xferargs);
        send_msg(current_client, "UserNotFound", 13);
        return 0;
    }

    current_client->file_transfers = xferargs;
    xferargs->operation = SENDING_OP;
    xferargs->target_type = USER_TARGET;

    //Generate an unique token for this transfer
    generate_token(xferargs->token, TRANSFER_TOKEN_SIZE);

    //Send out a file transfer request to the target
    sprintf(buffer, "!sendfile=%s,size=%zu,crc=%x,target=%s,token=%s", 
            xferargs->filename, xferargs->filesize, xferargs->checksum, current_client->username, xferargs->token);
    printf("Forwarding file transfer request from user \"%s\" to  user \"%s\", for file \"%s\" (%zu bytes, token: %s, checksum: %x)\n", 
            current_client->username, xferargs->target_user->username, xferargs->filename, xferargs->filesize, xferargs->token, xferargs->checksum);

    send_msg(xferargs->target_user->c, buffer, strlen(buffer)+1);
    send_msg(current_client, "Delivered", 10);

    //Set a timeout event for the request
    xferargs->timeout = calloc(1, sizeof(TimerEvent));
    xferargs->timeout->event_type = EXPIRING_TRANSFER_REQ;
    xferargs->timeout->c = current_client;

    //Create a timerfd to keep track of this transfer invite's expiry
    xferargs->timeout->timerfd = create_timerfd(XFER_REQUEST_TIMEOUT, 0, timers_epollfd);
    if(!xferargs->timeout->timerfd)
    {
        free(xferargs);
        return 0;
    }
    HASH_ADD_INT(timers, timerfd, xferargs->timeout);

    return 1;
}

int accepted_file_transfer()
{
    char target_username[USERNAME_LENG+1];
    XferTarget target_ret;
    User *target;
    FileXferArgs_Server *xferargs = calloc(1, sizeof(FileXferArgs_Server));

    sscanf(buffer, "!acceptfile=%[^,],size=%zu,crc=%x,target=%[^,],token=%s", 
            xferargs->filename, &xferargs->filesize, &xferargs->checksum, target_username, xferargs->token);
    printf("User \"%s\" has accepted the file \"%s\" (%zu bytes, token: %s, checksum: %x) from user \"%s\"\n", 
            current_client->username, xferargs->filename, xferargs->filesize, xferargs->token, xferargs->checksum, target_username);

    xferargs->operation = RECVING_OP;
    xferargs->target_type = USER_TARGET;

    //Matches with the target user's transfer information with the info provided in the request
    memset(&target_ret, 0, sizeof(XferTarget));
    if(!validate_transfer_target(xferargs, current_client->username, target_username, &target_ret))
    {
        printf("Transfer information mismatched. Cancelling...\n");
        send_msg(current_client, "WrongInfo", 10);
        free(xferargs);
        return 0;
    }

    if(target_ret.target_type != USER_TARGET)
    {
        printf("Target is not a user. Cancelling...\n");
        send_msg(current_client, "WrongInfo", 10);
        free(xferargs);
        return 0;
    }

    target = target_ret.user;
    xferargs->target_user = target;
    current_client->file_transfers = xferargs;

    //Cancel the timeout timer on the sender side
    cleanup_timer_event(target->c->file_transfers->timeout);
    target->c->file_transfers->timeout = NULL;
    
    //Forward the accept message to the sender
    sprintf(buffer, "!acceptfile=%s,size=%zu,crc=%x,target=%s,token=%s", 
            xferargs->filename, xferargs->filesize, xferargs->checksum, current_client->username, xferargs->token);
    return send_msg(xferargs->target_user->c, buffer, strlen(buffer)+1);
}


int rejected_file_transfer()
{
    char target_name[USERNAME_LENG+1];
    char reason[MAX_MSG_LENG+1];
    User *target;

    sscanf(buffer, "!rejectfile=%[^,],reason=%s", target_name, reason);

    HASH_FIND_STR(active_users, target_name, target);
    if(!target || !target->c->file_transfers || strcmp(target->c->file_transfers->target_user->username, current_client->username) != 0)
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
    send_msg(current_client->file_transfers->target_user->c, buffer, strlen(buffer)+1);    
    cancel_user_transfer(current_client);

    return 1;
}


/* FORWARDING FILE PIECES */

static int group_send_next_piece();
static int group_recv_next_piece();

//The receiver is ready for receiving the next piece (EPOLLOUT received)
int client_data_forward_recver_ready()
{
    FileXferArgs_Server *xferargs = current_client->file_transfers;
    FileXferArgs_Server *sender_xferargs;
    size_t bytes_remaining;
    int bytes_sent;

    if(!xferargs)
        return 0;

    if(xferargs->target_type == GROUP_TARGET)
        return group_send_next_piece();
    
    sender_xferargs = current_client->file_transfers->target_user->c->file_transfers;

    //Nothing to send if the last piece has been completely forwarded
    if(sender_xferargs->piece_size == 0)
    {
        //Rearm epoll notifications for both sender and receiver for the next piece
        update_epoll_events(connections_epollfd, sender_xferargs->xfer_socketfd, XFER_SENDER_EPOLL_EVENTS);
        update_epoll_events(connections_epollfd, current_client->socketfd, XFER_RECVER_EPOLL_EVENTS);
        return 0;
    }
       
    bytes_remaining = sender_xferargs->piece_size - sender_xferargs->piece_transferred;
    if(bytes_remaining <= 0)
    {
        sender_xferargs->piece_size = 0;
        sender_xferargs->piece_transferred = 0;
        return 0;
    }

    bytes_sent = send_direct(current_client->socketfd, &sender_xferargs->piece_buffer[sender_xferargs->piece_transferred], bytes_remaining);
    //bytes_sent = send_direct(current_client->socketfd, &sender_xferargs->piece_buffer[sender_xferargs->piece_transferred], (LONG_RECV_PAGE_SIZE > bytes_remaining)? bytes_remaining:LONG_RECV_PAGE_SIZE);
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

    if(xferargs->transferred >= xferargs->filesize)
        printf("All bytes for file transfer has been forwarded. Waiting for receiver \"%s\" to close the connection...\n", xferargs->target_user->username);

    //Rearm epoll notifications for receiver (to receive the next chunk/piece, or to close the connection when complete)
    update_epoll_events(connections_epollfd, current_client->socketfd, XFER_RECVER_EPOLL_EVENTS);

    return bytes_sent;
}


//The sender has a new piece ready (EPOLLIN received)
int client_data_forward_sender_ready()
{
    FileXferArgs_Server *xferargs = current_client->file_transfers;
    FileXferArgs_Server *recver_xferargs;
    int bytes_recvd;

    //Do not receive a new piece from the sender if the last piece hasn't been fully forwarded yet
    if(!xferargs || xferargs->piece_size > 0)
        return 0;
    
    if(xferargs->target_type == GROUP_TARGET)
    {
        bytes_recvd = group_recv_next_piece();
        if(!bytes_recvd)
        {
            disconnect_client(current_client);
            return 0;
        }
        
        return bytes_recvd;
    }
        

    //Receive a new piece of data that was sent by the sender, if the old piece has been completely forwarded already
    bytes_recvd = recv_direct(current_client->socketfd, xferargs->piece_buffer, XFER_BUFSIZE);
    if(!bytes_recvd)
        return 0;

    //If the receiver is a user, rearm the receiver's epoll events and forward the piece to the receiver when ready
    xferargs->piece_size = bytes_recvd;
    recver_xferargs = current_client->file_transfers->target_user->c->file_transfers;
    update_epoll_events(connections_epollfd, recver_xferargs->xfer_socketfd, XFER_RECVER_EPOLL_EVENTS);

   return bytes_recvd;
}



/******************************/
/* Client-Group File Sharing */
/******************************/ 

int put_new_file_to_group()
{
    FileXferArgs_Server *xferargs = calloc(1, sizeof(FileXferArgs_Server));
    Group_Member *target_member;

    if(!msg_target)
        return 0;
    msg_target += 2;
    
    sscanf(msg_body, "!putfile=%[^,],size=%zu,crc=%x", 
            xferargs->filename, &xferargs->filesize, &xferargs->checksum);

    //Check if group exists and user is a member
    if(!basic_group_permission_check(msg_target, &xferargs->target_group, &target_member))
    {
        free(xferargs);
        return 0;
    }

    //Check if group allows file transfers and the user has such permission.
    if( !(xferargs->target_group->group_flags & GRP_FLAG_ALLOW_XFER) || !(target_member->permissions & GRP_PERM_CAN_PUTFILE) )
    {
        printf("User \"%s\" is not permitted to upload files to group \"%s\"\n", current_client->username, msg_target);
        send_msg(current_client, "NoPermission", 13);
        free(xferargs);
        return 0;
    }

    //Generate an unique token for this transfer
    generate_token(xferargs->token, TRANSFER_TOKEN_SIZE);
    
    xferargs->myself = get_current_client_user();
    xferargs->operation = SENDING_OP;
    xferargs->target_type = GROUP_TARGET; 
    current_client->file_transfers = xferargs;

    //Send out a notification to the group
    sprintf(buffer, "User \"%s\" is uploading a file \"%s\" (%zu bytes, crc: %x) to the group \"%s\"...", 
            current_client->username, xferargs->filename, xferargs->filesize, xferargs->checksum, xferargs->target_group->groupname);
    printf("%s\n", buffer);
    send_group(xferargs->target_group, buffer, strlen(buffer)+1);

    //Accept the file
    if(!make_folder_and_file_for_writing(GROUP_XFER_ROOT, xferargs->target_group->groupname, xferargs->filename, xferargs->target_file, &xferargs->file_fp))
        return 0;

    sprintf(buffer, "!acceptfile=%s,size=%zu,crc=%x,target=%s,token=%s", 
            xferargs->filename, xferargs->filesize, xferargs->checksum, xferargs->target_group->groupname, xferargs->token);
    
    return send_msg(current_client, buffer, strlen(buffer)+1);
}


int get_new_file_from_group()
{
    FileXferArgs_Server *xferargs = calloc(1, sizeof(FileXferArgs_Server));
    Group_Member *target_member;
    
    File_List *requested_file;
    unsigned int requested_fileid;

    if(!msg_target)
        return 0;
    msg_target += 2;
    
    sscanf(msg_body, "!getfile %u", &requested_fileid);

    //Check if group exists and user is a member
    if(!basic_group_permission_check(msg_target, &xferargs->target_group, &target_member))
    {
        free(xferargs);
        return 0;
    }

    //Check if group allows file transfers and the user has such permission.
    if( !(xferargs->target_group->group_flags & GRP_FLAG_ALLOW_XFER) || !(target_member->permissions & GRP_PERM_CAN_GETFILE) )
    {
        printf("User \"%s\" is not permitted to download files from group \"%s\"\n", current_client->username, msg_target);
        send_msg(current_client, "NoPermission", 13);
        free(xferargs);
        return 0;
    }
    
    //Find the associated file's information by the fileid
    HASH_FIND_INT(xferargs->target_group->filelist, &requested_fileid, requested_file);
    if(!requested_file)
    {
        printf("Could not find a file associated with fileid %u in group \"%s\"\n", requested_fileid, xferargs->target_group->groupname);
        free(xferargs);
        send_msg(current_client, "WrongInfo", 10);
        return 0;
    }

    //Generate an unique token for this transfer
    generate_token(xferargs->token, TRANSFER_TOKEN_SIZE);

    //Copy the file information found into my xferargs
    strcpy(xferargs->target_file, requested_file->target_file);
    strcpy(xferargs->filename, requested_file->filename);
    xferargs->filesize = requested_file->filesize;
    xferargs->checksum = requested_file->checksum;

    xferargs->myself = get_current_client_user();
    xferargs->operation = RECVING_OP;
    xferargs->target_type = GROUP_TARGET; 
    current_client->file_transfers = xferargs;


    //Open the target file for reading
    xferargs->file_fp = fopen(xferargs->target_file, "rb");
    if(!xferargs->file_fp)
    {
        perror("Failed to open file for sending.");
        return 0;
    }
    
    //Open the file with mmap for reading
    xferargs->file_buffer = mmap(NULL, xferargs->filesize, PROT_READ, MAP_SHARED, fileno(xferargs->file_fp), 0);
    if(xferargs->file_buffer == MAP_FAILED)
    {
        perror("Failed to map sending file to memory.");
        fclose(xferargs->file_fp);
        return 0;
    }

    sprintf(buffer, "!getfile=%s,size=%zu,crc=%x,target=%s,token=%s", 
        xferargs->filename, xferargs->filesize, xferargs->checksum, xferargs->target_group->groupname, xferargs->token);
    
    return send_msg(current_client, buffer, strlen(buffer)+1);
}


//For ongoing getfile operations
static int group_send_next_piece()
{
    FileXferArgs_Server *xferargs = current_client->file_transfers;
    size_t bytes_remaining = xferargs->filesize - xferargs->transferred;
    int bytes_sent;
    
    bytes_sent = send_direct(current_client->socketfd, &xferargs->file_buffer[xferargs->transferred], bytes_remaining);
    //bytes_sent = send_direct(current_client->socketfd, &xferargs->file_buffer[xferargs->transferred], (LONG_RECV_PAGE_SIZE > bytes_remaining)? bytes_remaining:LONG_RECV_PAGE_SIZE);
    if(bytes_sent < 0)
    {
        perror("Failed to send the current piece");
        return -1;
    }
    //sleep(1);

    xferargs->transferred += bytes_sent;

    //Has the entire file been sent yet?
    if(xferargs->transferred >= xferargs->filesize)
    {
        printf("All bytes for file transfer has been forwarded. Waiting for receiver \"%s\" to close the connection...\n", xferargs->target_user->username);
        update_epoll_events(connections_epollfd, current_client->socketfd, EPOLLRDHUP);
    }
    else
        update_epoll_events(connections_epollfd, current_client->socketfd, XFER_RECVER_EPOLL_EVENTS);
    
    return 1;
}


//For ongoing putfile operations
static int group_recv_next_piece()
{
    FileXferArgs_Server *xferargs = current_client->file_transfers;
    int bytes_recvd;

    //Receive a new piece of data that was sent by the sender, if the old piece has been completely forwarded already
    bytes_recvd = recv_direct(current_client->socketfd, xferargs->piece_buffer, XFER_BUFSIZE);
    if(!bytes_recvd)
        return 0;

    //Save the new piece to the target file
    if(write(fileno(xferargs->file_fp), xferargs->piece_buffer, bytes_recvd) != bytes_recvd)
    {
        perror("Failed to write correct number of bytes to receiving file.");
        return 0;
    }

    //Did the file transfer complete?
    xferargs->transferred += bytes_recvd;
    if(xferargs->transferred >= xferargs->filesize)
    {
        if(!verify_received_file(xferargs->filesize, xferargs->checksum, xferargs->target_file))
            return 0;

        printf("Group: %s\n", xferargs->target_group->groupname);
        add_file_to_group(xferargs->target_group, xferargs->myself->username, xferargs->filename, xferargs->filesize, xferargs->checksum, xferargs->target_file);

        disconnect_client(current_client);
        return bytes_recvd;
    }

    //Rearm epoll notifications for sender (to send the next piece)
    update_epoll_events(connections_epollfd, xferargs->xfer_socketfd, XFER_SENDER_EPOLL_EVENTS);

    return bytes_recvd;
}