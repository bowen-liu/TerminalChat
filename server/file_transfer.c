#include "file_transfer.h"
#include "server.h"



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
    

    //Send out a file transfer request to the target
    sprintf(buffer, "!sendfile=%s,size=%zu,target=%s", args->filename, args->filesize, current_client->username);
    printf("Forwarding file transfer request from user \"%s\" to  user \"%s\", for file \"%s\" (%zu bytes)\n", current_client->username, args->target->username, args->filename, args->filesize);

    return send_msg(args->target->c, buffer, strlen(buffer)+1);
}

int accepted_file_transfer()
{
    char accepted_filename[FILENAME_MAX+1];
    char accepted_sender_name[USERNAME_LENG+1];
    size_t accepted_filesize;
    User *sender;
    int matches = 0;
    
    sscanf(buffer, "!acceptfile=%[^,],size=%zu,target=%s", accepted_filename, &accepted_filesize, accepted_sender_name);
    printf("User \"%s\" has accepted the file \"%s\" (%zu bytes) from user \"%s\"\n", current_client->username, accepted_filename, accepted_filesize, accepted_sender_name);

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
    
    //Forward the accept message to the sender
    sprintf(buffer, "!acceptfile=%s,size=%zu,target=%s", accepted_filename, accepted_filesize, current_client->username);
    return send_msg(sender->c, buffer, strlen(buffer)+1);
}

