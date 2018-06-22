#include "commands.h"
#include "client.h"

/******************************/
/*   Client-side Operations   */
/******************************/

int handle_user_command()
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

void parse_error_code()
{
    unsigned int err;
    char *additional_info;

    sscanf(buffer, "!err=%u", &err);
    additional_info = strchr(buffer, ',');

    switch(err)
    {
        case ERR_NONE:
            return;

        case ERR_INVALID_CMD:
            printf("Invalid command specified");
            break;

        case ERR_INVALID_NAME: 
            printf("Invalid name specified");
            break;

        case ERR_USER_NOT_FOUND: 
            printf("Targe user was not found");
            break;

        case ERR_GROUP_NOT_FOUND:
            printf("Targe group was not found"); 
            break;

        case ERR_NO_PERMISSION: 
            printf("You do not have enough permission to perform that action");
            break;

        case ERR_ALREADY_JOINED: 
            printf("Target user has already joined the group");
            break;

        case ERR_IP_BANNED:
            printf("You cannot perform that action, because you have been IP banned");
            break;

        case ERR_INCORRECT_INFO: 
            printf("Information provided does not match the server");
            break;

        case ERR_NO_XFER_FOUND:
            printf("Target has no valid pending transfer with you");
            break;

        default:
            printf("Unknown error %u", err);
    }

    if(additional_info)
        printf(": \"%s\"\n", ++additional_info);
    else
        printf(".\n");
}

void parse_control_message(char* cmd_buffer)
{
    char *old_buffer = buffer;
    buffer = cmd_buffer;
    
    if(strncmp("!err=", buffer, 5) == 0)
        parse_error_code();

    /*Group operations. Implemented in group.c*/

    else if(strncmp("!userlist=", buffer, 10) == 0)
        parse_userlist();

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
