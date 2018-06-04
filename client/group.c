#include "group.h"
#include "client.h"


Namelist* groups_joined;                //List of all groups I'm currently joined


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
    sprintf(buffer, "!joingroup=%s", group_name);
    send_msg_client(my_socketfd, buffer, strlen(buffer)+1);
}

void group_joined()
{
    Namelist *groupname = malloc(sizeof(Namelist));

    sscanf(buffer, "!groupjoined=%s", groupname->name);
    printf("You have joined the group \"%s\".\n", groupname->name);

    //Record the invited group into the list of participating groups
    LL_APPEND(groups_joined, groupname);
}

void group_kicked()
{
    char group_name[USERNAME_LENG+1], kicked_by[USERNAME_LENG+1];
    Namelist *current_group_name, *tmp;

    sscanf(buffer, "!groupkicked=%[^,],by=%s", group_name, kicked_by);
    printf("You have been kicked out of the group \"%s\" by user \"%s\".\n", group_name, kicked_by);

    //Find the entry in the client's group_joined list and remove the entry
    LL_FOREACH_SAFE(groups_joined, current_group_name, tmp)
    {
        if(strcmp(group_name, current_group_name->name) == 0)
            break;
        else 
            current_group_name = NULL;
    }

    if(current_group_name)
    {
        LL_DELETE(groups_joined, current_group_name);
        free(current_group_name);
    }
    else
        printf("You do not appear to be a member of the group \"%s\".\n", group_name);
}


void user_left_group()
{
    char groupname[USERNAME_LENG+1], username[USERNAME_LENG+1];

    sscanf(buffer, "!leftgroup=%[^,],user=%s", groupname, username);
    printf("User \"%s\" has left the group \"%s\".\n", username, groupname);
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
    char groupname[USERNAME_LENG+1];
    Namelist *current_group_name, *tmp_name;

    sscanf(buffer, "!leavegroup=%s", groupname);

    //Find the entry in the client's group_joined list and remove the entry
    LL_FOREACH_SAFE(groups_joined, current_group_name, tmp_name)
    {
        if(strcmp(groupname, current_group_name->name) == 0)
            break;
        else 
            current_group_name = NULL;
    }

    if(!current_group_name)
    {
        printf("You do not appear to be a member of the group \"%s\".\n", groupname);
        return 1;       //Send the leave command to server anyways, just in case if we didn't record the join
    }

    printf("You have left the group \"%s\".\n", groupname);
    LL_DELETE(groups_joined, current_group_name);
    free(current_group_name);
    return 1;
}