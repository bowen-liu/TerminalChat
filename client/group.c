#include "group.h"
#include "client.h"


/******************************/
/*  Server Control Messages   */
/******************************/

//TODO: Maybe allow the user to choose to decline an invitation?
void group_invited()
{
    char group_name[USERNAME_LENG+1], invite_sender[USERNAME_LENG+1];

    sscanf(buffer, "!groupinvite=%[^,],sender=%s", group_name, invite_sender);
    printf("You are being invited to the group \"%s\" by user \"%s\".\n", group_name, invite_sender);

    //Automatically accept it for now
    sprintf(buffer, "!join @@%s", group_name);
    send_msg_client(buffer, strlen(buffer)+1);
}

void group_joined()
{
    char groupname[USERNAME_LENG+1];
    
    sscanf(buffer, "!groupjoined=%s", groupname);
    printf("You have joined the group \"%s\".\n", groupname);
}

void group_kicked()
{
    char group_name[USERNAME_LENG+1], kicked_by[USERNAME_LENG+1], reason[DISCONNECT_REASON_LENG+1];

    sscanf(buffer, "!groupkicked=%[^,],by=%[^,],reason=%[^$]", group_name, kicked_by, reason);
    printf("You have been kicked from the group \"%s\" by user \"%s\". Reason: %s\n", group_name, kicked_by, reason);
}

void group_banned()
{
    char group_name[USERNAME_LENG+1], kicked_by[USERNAME_LENG+1], reason[DISCONNECT_REASON_LENG+1];

    sscanf(buffer, "!groupbanned=%[^,],by=%[^,],reason=%[^$]", group_name, kicked_by, reason);
    printf("You have been banned from the group \"%s\" by user \"%s\". Reason: %s\n", group_name, kicked_by, reason);
}


void user_left_group()
{
    char groupname[USERNAME_LENG+1], username[USERNAME_LENG+1], reason[DISCONNECT_REASON_LENG+1];

    sscanf(buffer, "!leftgroup=%[^,],user=%[^,],reason=%[^$]", groupname, username, reason);
    printf("User \"%s\" has left the group \"%s\". ", username, groupname);

    if(strcmp(reason, "none") == 0)
        printf("\n");
    else
        printf("Reason: %s\n", reason);
}


void user_joined_group()
{
    char groupname[USERNAME_LENG+1], username[USERNAME_LENG+1];

    sscanf(buffer, "!joinedgroup=%[^,],user=%s", groupname, username);
    printf("User \"%s\" has joined the group \"%s\".\n", username, groupname);
}


int parse_filelist()
{
    char group_name[USERNAME_LENG+1];
    unsigned int file_count, i;
    unsigned int header_len;

    char *current_fileinfo = 0;
    unsigned int current_fileinfo_idx = 0;

    unsigned int fileid;
    char uploader[USERNAME_LENG+1];
    char filename[MAX_FILENAME+1];
    size_t filesize;

    //Parse the header
    sscanf(buffer, "!filelist=%u,group=%[^,],%n", &file_count, group_name, &header_len);
    printf("%u file(s) are available for download in the group \"%s\":\n", file_count, group_name);

    current_fileinfo_idx = header_len;
    
    //Extract each subsequent file's info from the message
    for(i=0; i<file_count; i++)
    {
        current_fileinfo = strchr(&buffer[current_fileinfo_idx], '[');
        if(!current_fileinfo)
            break;
        
        current_fileinfo_idx = current_fileinfo - buffer + 1;
        
        sscanf(current_fileinfo, "[%u,%[^,],%zu,%[^]]]", &fileid, filename, &filesize, uploader);
        printf("FileID: %u \t \"%s\" (%zu bytes) \t Uploader: %s\n", fileid, filename, filesize, uploader);
    }
     

    return file_count;
}


/******************************/
/*   Client-side Operations   */
/******************************/

int leaving_group()
{
    char groupname[USERNAME_LENG+3];
    char *groupname_plain;

    sscanf(buffer, "!leave %s", groupname);
    groupname_plain = plain_name(groupname);

    printf("You have left the group \"%s\".\n", groupname_plain);
    return 1;
}