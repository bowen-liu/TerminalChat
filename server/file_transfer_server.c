#include "file_transfer_server.h"
#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/******************************/
/*          Helpers           */
/******************************/ 

void print_server_xferargs(FileXferArgs_Server *args)
{
    if(!args)
    {
        printf("args are null!\n");
        return;
    }
    
    printf("Target: \"%s\", OP: %s, Filename: \"%s\", Filesize: %zu, Transferred: %zu, Token: %s\n", 
            args->target->username, (args->operation == SENDING_OP)? "Send":"Recv", args->filename, args->filesize, args->transferred, args->token);
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


int validate_transfer_target (FileXferArgs_Server *request, char* request_username, char* target_username)
{
    User *request_user, *target_user;
    FileXferArgs_Server *xferargs;
    enum sendrecv_op expected_target_op;
    int matches = 0;

    /* Validates the request against the requesting user's own information */

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
            if(strcmp(xferargs->filename, request->filename) == 0)
                if(xferargs->filesize == request->filesize)
                    if(strcmp(xferargs->token, request->token) == 0)
                        if(xferargs->transferred == 0)
                            matches = 1;
                        else
                            printf("Transfer already commencing\n");
                    else
                        printf("Wrong token\n");
                else
                    printf("Wrong filesize\n");
            else
                printf("Wrong filename\n");
        else
            printf("Wrong op\n");


        if(!matches)
        {
            printf("No matching request found with requester \"%s\".\n", request_username);
            print_server_xferargs(xferargs);
            return 0;
        }
    }
    else
        request_username = current_client->username;        //Use current_client if request_username is not not specified 


    /* Validates the request against the requesting user's own information */
    
    matches = 0;

    //Locate the target user
    HASH_FIND_STR(active_users, target_username, target_user);
    if(!target_user)
    {
        printf("User \"%s\" not found\n", target_username);
        return 0;
    }
    request->target = target_user;


    //Match the information in the target user's transfer args with this request
    xferargs = target_user->c->file_transfers;
    expected_target_op = (request->operation == SENDING_OP)? RECVING_OP:SENDING_OP;

    if(xferargs && xferargs->operation == expected_target_op)
        if(strcmp(xferargs->filename, request->filename) == 0)
            if(xferargs->filesize == request->filesize)
                if(xferargs->transferred == 0)
                    if(strcmp(xferargs->target->username, request_username) == 0)
                        if(strcmp(xferargs->token, request->token) == 0)
                            matches = 1;

    if(!matches)
    {
        printf("No matching file found with sender \"%s\".\n", target_username);
        return 0;
    }

    return 1;
}



/******************************************/
/* New Incoming File Transfer Connections */
/******************************************/ 


int register_send_transfer_connection()
{
    printf("Got a new send connection request!");
    printf("%s\n", buffer);

    sprintf(buffer, "Accepted");
    send_msg(current_client, buffer, strlen(buffer)+1);
}

int register_recv_transfer_connection()
{
    char sender_name[USERNAME_LENG+1], recver_name[USERNAME_LENG+1];
    User *sender, *recver;

    FileXferArgs_Server *xferargs = calloc(1, sizeof(FileXferArgs_Server));

    printf("Got a new recv connection request!\n");

    xferargs->operation = RECVING_OP;
    sscanf(buffer, "!xferrecv=%[^,],size=%zu,sender=%[^,],recver=%[^,],token=%[^,]", 
            xferargs->filename, &xferargs->filesize, sender_name, recver_name, xferargs->token);


    if(!validate_transfer_target(xferargs, recver_name, sender_name))
    {
        printf("Mismatching RECEIVING transfer connection.\n");
        send_msg(current_client, "WrongInfo", 10);
        disconnect_client(current_client);
        free(xferargs);
        return 0;
    }

    current_client->connection_type = TRANSFER_CONNECTION;
    current_client->file_transfers = xferargs;
    strcpy(current_client->username, recver_name);
    

    sprintf(buffer, "Accepted");
    send_msg(current_client, buffer, strlen(buffer)+1);
    printf("Accepted RECEIVING transfer connection for file \"%s\" (%zu bytes, token: %s), from \"%s\" to \"%s\".\n", 
            xferargs->filename, xferargs->filesize, xferargs->token, sender_name, recver_name);

    return 0;
}


/******************************/
/* Client-Client File Sharing */
/******************************/ 

int client_transfer()
{
    return 1;
}

int new_client_transfer()
{
    FileXferArgs_Server *args = calloc(1, sizeof(FileXferArgs_Server));
    char target_name[USERNAME_LENG+1];

    sscanf(buffer, "!sendfile=%[^,],size=%zu,target=%s", args->filename, &args->filesize, target_name);

    //Find the target user specified
    HASH_FIND_STR(active_users, target_name, args->target);
    if(!args->target)
    {
        printf("User \"%s\" not found\n", target_name);
        free(args);
        send_msg(current_client, "UserNotFound", 13);
        return 0;
    }

    current_client->file_transfers = args;
    args->operation = SENDING_OP;

    //Generate an unique token for this transfer
    generate_token(args->token, TRANSFER_TOKEN_SIZE);

    //Send out a file transfer request to the target
    sprintf(buffer, "!sendfile=%s,size=%zu,target=%s,token=%s", args->filename, args->filesize, current_client->username, args->token);
    printf("Forwarding file transfer request from user \"%s\" to  user \"%s\", for file \"%s\" (%zu bytes, token: %s)\n", 
            current_client->username, args->target->username, args->filename, args->filesize, args->token);

    return send_msg(args->target->c, buffer, strlen(buffer)+1);
}

int accepted_file_transfer()
{
    char target_username[USERNAME_LENG+1];
    FileXferArgs_Server *xferargs = calloc(1, sizeof(FileXferArgs_Server));

    sscanf(buffer, "!acceptfile=%[^,],size=%zu,target=%[^,],token=%s", xferargs->filename, &xferargs->filesize, target_username, xferargs->token);
    printf("User \"%s\" has accepted the file \"%s\" (%zu bytes, token: %s) from user \"%s\"\n", 
            current_client->username, xferargs->filename, xferargs->filesize, xferargs->token, target_username);

    xferargs->operation = RECVING_OP;

    //Matches with the target user's transfer information
    if(!validate_transfer_target (xferargs, NULL, target_username))
    {
        printf("Transfer information mismatched. Cancelling...\n");
        send_msg(current_client, "WrongInfo", 10);
        free(xferargs);
        return 0;
    }

    current_client->file_transfers = xferargs;
    
    //Forward the accept message to the sender
    sprintf(buffer, "!acceptfile=%s,size=%zu,target=%s,token=%s", xferargs->filename, xferargs->filesize, current_client->username, xferargs->token);
    return send_msg(xferargs->target->c, buffer, strlen(buffer)+1);
}

