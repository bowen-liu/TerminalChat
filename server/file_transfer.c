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
    FileXferArgs args;
    User *receiver;

    args = parse_send_cmd_recver(buffer);
    args.socketfd = current_client->socketfd;

    //Find the target user specified
    HASH_FIND_STR(active_users, args.target_name, receiver);
    if(!receiver)
    {
        printf("User \"%s\" not found\n", args.target_name);
        send_msg(current_client, "UserNotFound", 13);
        return 0;
    }

    //Send out a file transfer request to the target
    sprintf(buffer, "!sendfile=%s,size=%zu,target=%s", args.filename, args.filesize, current_client->username);
    printf("Forwarding file transfer request from user \"%s\" to  user \"%s\", for file \"%s\" (%zu bytes)\n", current_client->username, args.target_name, args.filename, args.filesize);

    return send_msg(receiver->c, buffer, strlen(buffer)+1);
}

