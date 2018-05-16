#include "file_transfer_server.h"
#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/******************************/
/*          Helpers           */
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
            print_server_xferargs(xferargs);
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
        free(myself->c->file_transfers);
        return 0;
    }

    //Update my original FileXferArgs
    xferargs = myself->c->file_transfers;
    xferargs->myself = myself;
    xferargs->xfer_socketfd =  current_client->socketfd;
    xferargs->operation = SENDING_OP;
    xferargs->target = target;

    current_client->connection_type = TRANSFER_CONNECTION;
    current_client->file_transfers = xferargs;

    sprintf(buffer, "Accepted");
    send_msg(current_client, buffer, strlen(buffer)+1);
    printf("Accepted SENDING transfer connection for file \"%s\" (%zu bytes, token: %s, checksum: %x), from \"%s\" to \"%s\".\n",
            xferargs->filename, xferargs->filesize, xferargs->token, xferargs->checksum, sender_name, recver_name);
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
        free(myself->c->file_transfers);
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

    return 0;
}



/******************************/
/* Client-Client File Sharing */
/******************************/ 

int new_client_transfer()
{
    FileXferArgs_Server *xferargs = calloc(1, sizeof(FileXferArgs_Server));
    char target_name[USERNAME_LENG+1];

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

    return send_msg(xferargs->target->c, buffer, strlen(buffer)+1);
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
    
    //Forward the accept message to the sender
    sprintf(buffer, "!acceptfile=%s,size=%zu,crc=%x,target=%s,token=%s", 
            xferargs->filename, xferargs->filesize, xferargs->checksum, current_client->username, xferargs->token);
    return send_msg(xferargs->target->c, buffer, strlen(buffer)+1);
}


int client_data_forward(char *buffer, size_t bytes)
{
    int recver_xfer_socketfd;
    char *myname = current_client->file_transfers->myself->username;
    int bytes_sent;

    if(current_client->file_transfers->operation == RECVING_OP)
    {
        printf("Received a message from \"%s\" RECV xfer connection. Ignoring...\n", myname);
        return 0;
    }
    
    recver_xfer_socketfd = current_client->file_transfers->target->c->file_transfers->xfer_socketfd;
    current_client->file_transfers->transferred += bytes;

    printf("Forwarding %zu bytes (%zu\\%zu) from \"%s\" to \"%s\".\n", 
            bytes, current_client->file_transfers->transferred, current_client->file_transfers->filesize, myname, current_client->file_transfers->target->username);
    
    bytes_sent = send_msg_direct(recver_xfer_socketfd, buffer, bytes);

    //Close the sender's connection if the entire file has been forwarded
    if(current_client->file_transfers->transferred == current_client->file_transfers->filesize)
    {
        printf("All bytes for file transfer has been forwarded.\n");
        disconnect_client(current_client);
    }

    return bytes_sent;
}

