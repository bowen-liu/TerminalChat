#include "file_transfer_server.h"
#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/******************************/
/*          Helpers         */
/******************************/ 

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
    printf("Forwarding file transfer request from user \"%s\" to  user \"%s\", for file \"%s\" (%zu bytes, token: %s)\n", current_client->username, args->target->username, args->filename, args->filesize, args->token);

    return send_msg(args->target->c, buffer, strlen(buffer)+1);
}

int accepted_file_transfer()
{
    char accepted_filename[FILENAME_MAX+1];
    char accepted_sender_name[USERNAME_LENG+1];
    size_t accepted_filesize;
    char accepted_token[TRANSFER_TOKEN_SIZE+1];
    User *sender;
    int matches = 0;
    
    sscanf(buffer, "!acceptfile=%[^,],size=%zu,target=%[^,],token=%s", accepted_filename, &accepted_filesize, accepted_sender_name, accepted_token);
    printf("User \"%s\" has accepted the file \"%s\" (%zu bytes, token: %s) from user \"%s\"\n", current_client->username, accepted_filename, accepted_filesize, accepted_token, accepted_sender_name);

    //Match the file information that's stored in the sender's client descriptor
    HASH_FIND_STR(active_users, accepted_sender_name, sender);
    if(!sender)
    {
        printf("User \"%s\" not found\n", accepted_sender_name);
        send_msg(current_client, "UserNotFound", 13);
        return 0;
    }

    if(sender->c->file_transfers && sender->c->file_transfers->operation == SENDING_OP)
        if(strcmp(sender->c->file_transfers->filename, accepted_filename) == 0)
            if(sender->c->file_transfers->filesize == accepted_filesize)
                if(strcmp(sender->c->file_transfers->target->username, current_client->username) == 0)
                    if(strcmp(sender->c->file_transfers->token, accepted_token) == 0)
                        matches = 1;
    
    if(!matches)
    {
        printf("No matching file found with sender \"%s\".\n", accepted_sender_name);
        send_msg(current_client, "NoPermission", 13);
        return 0;
    }

    current_client->file_transfers = calloc(1, sizeof(FileXferArgs_Server));
    current_client->file_transfers->target = sender;
    current_client->file_transfers->operation = RECVING_OP;
    current_client->file_transfers->filesize = accepted_filesize;
    strcpy(current_client->file_transfers->filename, accepted_filename);
    strcpy(current_client->file_transfers->token, accepted_token);
    
    //Forward the accept message to the sender
    sprintf(buffer, "!acceptfile=%s,size=%zu,target=%s,token=%s", accepted_filename, accepted_filesize, current_client->username, accepted_token);
    return send_msg(sender->c, buffer, strlen(buffer)+1);
}

