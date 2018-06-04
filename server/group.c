#include "server_common.h"
#include "group.h"
#include "server.h"

/******************************/
/*         Group Send         */
/******************************/

unsigned int send_group(Group* group, char* buffer, size_t size)
{
    unsigned int members_sent = 0;
    Group_Member *curr = NULL, *tmp;

    HASH_ITER(hh, group->members, curr, tmp) 
    {
        //Do not send if the current member was invited but hasn't joined the group 
        if(!(curr->permissions & GRP_PERM_HAS_JOINED))
            continue;
        
        if(!send_msg(curr->c, buffer, size))
            printf("Send failed to user \"%s\" in group \"%s\"\n", curr->username, group->groupname);
        else
            ++members_sent;
    }

    return members_sent;
}

int group_msg()
{
    char *target_group;
    char *target_msg;
    char gmsg[MAX_MSG_LENG];
    Group *target;
    Group_Member *sending_member;

    //Find the occurance of the first space
    target_msg = strchr(buffer, ' ');
    if(!target_msg)
    {
        printf("Invalid group specified, or no message specified\n");
        return 0;
    }

    //Seperate the target's name and message from the buffer
    target_group = &buffer[2];
    target_msg[0] = '\0';              //Mark the space separating the username and message as NULL, so we can directly read the username from the pointer
    target_msg += sizeof(char);        //Increment the message pointer by 1 to skip to the actual message

    //Find if the requested group currently exists
    HASH_FIND_STR(groups, target_group, target);
    if(!target)
    {
        printf("Group \"%s\" not found\n", target_group);
        send_msg(current_client, "InvalidGroup", 13);
        return 0;
    }

    //Checks if the sender is part of the group, or has enough permission to send group messages
    HASH_FIND_STR(target->members, current_client->username, sending_member);
    if(!sending_member)
    {
        printf("User \"%s\" is not a member of \"%s\"\n", current_client->username, target_group);
        send_msg(current_client, "NoPermission", 13);
        return 0;
    }
    else if((sending_member->permissions & (GRP_PERM_HAS_JOINED | GRP_PERM_CAN_TALK)) != (GRP_PERM_HAS_JOINED | GRP_PERM_CAN_TALK))
    {
        printf("User \"%s\" do not have permission to message group \"%s\"\n", current_client->username, target_group);
        send_msg(current_client, "NoPermission", 13);
        return 0;
    }

    //Forward message to the target
    sprintf(gmsg, "%s (%s): %s", current_client->username, target->groupname, target_msg);
    return send_group(target, gmsg, strlen(gmsg)+1);
}


/******************************/
/*      Group Helpers    */
/******************************/


static Group_Member* allocate_group_member(Group *group, Client *target_user, int permissions)
{
    Group_Member *newmember;
    Namelist *newgroup_entry;

    //Allocate a new Group Member object
    newmember = calloc(1, sizeof(Group_Member));
    strcpy(newmember->username, target_user->username);
    newmember->c = target_user;
    newmember->permissions = permissions;

    //Add the new user's entry to the group's userlist
    HASH_ADD_STR(group->members, c->username, newmember);

    //Record the participation of this group for the member's client descriptors
    newgroup_entry = calloc(1, sizeof(Namelist));
    strcpy(newgroup_entry->name, group->groupname);
    LL_APPEND(target_user->groups_joined, newgroup_entry);

    return newmember;

    //The member's associated Group_Member and Namelist objects must be freed when the member leaves the group!!
}


static void remove_group(Group *group)
{
    unsigned int mcount = HASH_COUNT(group->members), rcount = 0;

    Group_Member *cur_member = NULL, *tmp_member;
    Namelist *groupname_entry;

    File_List *cur_file, *tmp_file;
    char group_files_directory[MAX_FILE_PATH+1];
    
    if(mcount > 0)
    {
        printf("Group \"%s\" is nonempty. Possibly contains unused invites. (hash_count: %d, member_count: %d) \n", group->groupname, mcount, group->member_count);
        
        //Remove any remaining member objects
        HASH_ITER(hh, group->members, cur_member, tmp_member) 
        {
            printf("Removing unused invite for user \"%s\".\n", cur_member->username);

            groupname_entry = find_from_namelist(cur_member->c->groups_joined, group->groupname);
            if(groupname_entry)
            {
                LL_DELETE(cur_member->c->groups_joined, groupname_entry);
                free(groupname_entry);
            }
            else
                printf("Group entry was not found in client's descriptor\n");

            free(cur_member);
            ++rcount;
        }
        printf("Removed %u unresponded invites from group \"%s\"\n", rcount, group->groupname);
    }

    //Delete all files uploaded to this group
    HASH_ITER(hh, group->filelist, cur_file, tmp_file)
    {
        printf("Removing local file \"%s\" from deleted group \"%s\"\n", cur_file->target_file, group->groupname);
        
        if(remove(cur_file->target_file) < 0)
            perror("Failed to delete file.");

        free(cur_file);
    }
    
    sprintf(group_files_directory, "%s/%s", GROUP_XFER_ROOT, group->groupname);
    printf("Removing local folder \"%s\"\n", group_files_directory);
    if(remove(group_files_directory) < 0)
        perror("Failed to delete directorys.");

    HASH_DEL(groups, group);
    free(group);
}

void disconnect_client_group_cleanup(Client *c)
{
    Namelist *current_group_name, *tmp_name;
    Group *group;
    
    //Leave all participating chat groups.
    LL_FOREACH_SAFE(c->groups_joined, current_group_name, tmp_name)
    {
        printf("Disconnect: Leaving group \"%s\"\n", current_group_name->name);
        
        HASH_FIND_STR(groups, current_group_name->name, group);
        if(group)
        {
            leave_group_direct(group, current_client);
            LL_DELETE(current_client->groups_joined, current_group_name);
            free(current_group_name);
        }
    }
}

//Check if a group exists, and if the current requesting user is a member before fulfilling a request
int basic_group_permission_check(char *group_name, Group **group_ret, Group_Member **member_ret)
{
    Group *group;
    Group_Member *member;

    //Check if this group exists
    HASH_FIND_STR(groups, group_name, group);
    if(!group)
    {
        printf("Group \"%s\" not found\n", group_name);
        send_msg(current_client, "InvalidGroup", 13);
        return 0;
    }

    if(group_ret)
        *group_ret = group;
    
    //Check if the requesting user is a member
    HASH_FIND_STR(group->members, current_client->username, member);
    if(!member)
    {
        printf("User \"%s\" is not a member of the group \"%s\".\n", current_client->username, group_name);
        send_msg(current_client, "NoPermission", 13);
        return 0;
    }

    if(member_ret)
        *member_ret = member;
    
    return 1;
}


/******************************/
/*      Group Operations    */
/******************************/

int userlist_group(char *group_name)
{
    char* userlist_msg;
    size_t userlist_size = 0;

    Group *group = NULL;
    Group_Member *curr, *temp;
    
    if(!basic_group_permission_check(group_name, &group, NULL))
        return 0;

    userlist_msg = malloc(group->member_count * (USERNAME_LENG+1 + 128));
    sprintf(userlist_msg, "!userlist=%d,group=%s", group->member_count, group_name);

    //Iterate through the list of active usernames and append them to the buffer one at a time
    HASH_ITER(hh, group->members, curr, temp)
    {
        strcat(userlist_msg, ",");
        strcat(userlist_msg, curr->username);

        if((curr->permissions & GRP_PERM_ADMIN) == GRP_PERM_ADMIN)
            strcat(userlist_msg, " (admin)");
        
        if(!(curr->permissions & GRP_PERM_HAS_JOINED))
            strcat(userlist_msg, " (invited)");
    }
    userlist_size = strlen(userlist_msg) + 1;
    userlist_msg[userlist_size] = '\0';
    
    send_new_long_msg(userlist_msg, userlist_size);
    free(userlist_msg);

    return group->member_count;
}


static int invite_to_group_direct(Group *group, User *user);
int create_new_group()
{
    Group* newgroup = calloc(1, sizeof(Group));
    Group* groupname_exists = NULL;

    char *token;
    User *target_user = NULL;
    int invites_sent = 0;

    //Ensure the groupname is valid and does not already exist
    token = strtok(buffer, ",");
    sscanf(token, "!newgroup=%s", newgroup->groupname);

    if(!name_is_valid(newgroup->groupname))
    {
        send_msg(current_client, "InvalidGroupName", 17);
        free(newgroup);
        return 0;
    }

    HASH_FIND_STR(groups, newgroup->groupname, groupname_exists);
    if(groupname_exists)
    {
        printf("Group \"%s\" already exists.\n", newgroup->groupname);
        send_msg(current_client, "InvalidGroupName", 17);
        free(newgroup);
        return 0;
    }

    //Register the group
    HASH_ADD_STR(groups, groupname, newgroup);
    newgroup->group_flags = GRP_FLAG_DEFAULT;

    //Invite each of the clients to join the group
    token = current_client->username;
    while(token)
    {
        HASH_FIND_STR(active_users, token, target_user);
        if(!target_user)
        {
            printf("Could not find member \"%s\"\n", token);
            token = strtok(NULL, ",");
            continue;
        }

        //Record an invite for each initial member, and mark them as admins
        allocate_group_member(newgroup, target_user->c, GRP_PERM_ADMIN);
        
        if(invite_to_group_direct(newgroup, target_user))
            ++invites_sent;

        token = strtok(NULL, ",");
    }

    if(!invites_sent)
    {
        printf("Unable to invite any members to the new group. Cancelling...\n");
        remove_group(newgroup);
    }

    return invites_sent;
}


int leave_group_direct(Group *group, Client *c)
{
    char leavemsg[BUFSIZE];
    Group_Member* target_member;
    int has_joined;
    
    HASH_FIND_STR(group->members, c->username, target_member);
    if(!target_member)
    {
        printf("Leave: User \"%s\" not found in group \"%s\".\n", c->username, group->groupname);
        return 0;
    }

    has_joined = target_member->permissions & GRP_PERM_HAS_JOINED;
    HASH_DEL(group->members, target_member);
    free(target_member);

    //No need to announce member leaving if the member never joined the group (unresponded invite)
    if(has_joined)
    {
        --group->member_count;

        sprintf(leavemsg, "!leftgroup=%s,user=%s", group->groupname, c->username);
        printf("User \"%s\" has left the group \"%s\".\n", c->username, group->groupname);
        send_group(group, leavemsg, strlen(leavemsg)+1);
    }
    else
        printf("User \"%s\"'s unresponded invite has been removed from the group \"%s\".\n", c->username, group->groupname);

    //Delete this group if no members are remaining
    if(group->member_count == 0)
    {
        printf("Group \"%s\" is now empty. Deleting...\n", group->groupname);
        remove_group(group);
    }
        
    return 1;
}

int leave_group()
{
    char groupname[USERNAME_LENG+1];
    Namelist *groupname_entry;
    Group *group;

    sscanf(buffer, "!leavegroup=%s", groupname);

    //Locate this group in the hash table
    HASH_FIND_STR(groups, groupname, group);
    if(!group)
    {
        printf("Group \"%s\" was not found.\n", groupname);
        send_msg(current_client, "InvalidGroup", 13);
        return 0;
    }
    
    //Find the entry in the client's group_joined list and remove the entry
    groupname_entry = find_from_namelist(current_client->groups_joined, groupname);
    if(!groupname_entry)
    {
        printf("Leave: User \"%s\" not found in group \"%s\" (by joined_groups).\n", current_client->username, groupname);
        send_msg(current_client, "NoPermission", 13);
        return 0;
    }

    LL_DELETE(current_client->groups_joined, groupname_entry);
    free(groupname_entry);

    //Leave the group officially
    return leave_group_direct(group, current_client);
}


int join_group()
{
    char group_name[USERNAME_LENG+1];
    Group *group;
    Group_Member *newmember = NULL;
    
    sscanf(buffer, "!joingroup=%s", group_name);

    //Locate this group in the hash table
    HASH_FIND_STR(groups, group_name, group);
    if(!group)
    {
        printf("Group \"%s\" was not found.\n", group_name);
        send_msg(current_client, "InvalidGroup", 13);
        return 0;
    }

    //Check if a member entry already exists in this group
    HASH_FIND_STR(group->members, current_client->username, newmember);
    if(newmember)
    {
        if(newmember->permissions & GRP_PERM_HAS_JOINED)
        {
            printf("User \"%s\" is already a member of the group \"%s\". Permission: %d\n", current_client->username, group->groupname, newmember->permissions);
            send_msg(current_client, "AlreadyExist", 13);
            return 0;
        }
        
        //User has not joined the group yet, but has an invite reserved. Add the user directly to the group's userlist
        printf("User \"%s\" has a pending invitation to the group \"%s\".\n", current_client->username, group->groupname);
        newmember->permissions |= GRP_PERM_HAS_JOINED;
    }

    //If no invitation found, let the user join as a new regular member if the group isn't invite only.
    else
    {
        if(!(group->group_flags & GRP_FLAG_INVITE_ONLY))
            newmember = allocate_group_member(group, current_client, (GRP_PERM_HAS_JOINED | GRP_PERM_DEFAULT));
        else
        {
            printf("User \"%s\" wanted to join group \"%s\", but it's invite only.\n", current_client->username, group->groupname);
            send_msg(current_client, "InviteOnly", 11);
            return 0;
        }
    }
        

    printf("Added member \"%s\" to group \"%s\" %s\n", current_client->username, group->groupname, (newmember->permissions == (GRP_PERM_ADMIN | GRP_PERM_HAS_JOINED))? "as an admin":"");
    ++group->member_count;

    //Inform the new member that the join was sucessful
    sprintf(buffer, "!groupjoined=%s", group->groupname);
    if(!send_msg(current_client, buffer, strlen(buffer)+1))       //TODO: what if send failed?
        return 0;

    //Announce to other existing members that a new member has joined the group
    sprintf(buffer, "!joinedgroup=%s,user=%s", group->groupname, current_client->username);
    send_group(group, buffer, strlen(buffer)+1);

    return 1;
}


static int invite_to_group_direct(Group *group, User *user)
{
    char invite_msg[BUFSIZE];
    Group_Member *already_in_group;
    
    //Check if this user is already in the group
    HASH_FIND_STR(group->members, user->username, already_in_group);
    if(already_in_group && (already_in_group->permissions & GRP_PERM_HAS_JOINED))
    {
        printf("User \"%s\" is already a member of the group \"%s\".\n", user->username, group->groupname);
        send_msg(current_client, "AlreadyExist", 13);
        return 0;
    }

    //Announce to other existing members that a new member was invited
    sprintf(invite_msg, "User \"%s\" has been invited \"%s\" to join the group \"%s\"", current_client->username, user->username, group->groupname);
    printf("%s\n", invite_msg);
    send_group(group, invite_msg, strlen(invite_msg)+1);

    //Send an invite to the requested user to join the group
    sprintf(invite_msg, "!groupinvite=%s,sender=%s", group->groupname, current_client->username);
    if(!send_msg(user->c, invite_msg, strlen(invite_msg)+1))
        return 0;

    return 1;
}

int invite_to_group()
{
    char group_name[USERNAME_LENG+1];
    char *newbuffer = buffer, *token;
    Group* group;
    Group_Member *requesting_member;
    User* target_user;
    int users_invited = 0;
    

    token = strtok(newbuffer, ",");
    sscanf(token, "!invitegroup=%[^,]", group_name);

    if(!basic_group_permission_check(group_name, &group, &requesting_member))
        return 0;

    //Check if the member has permission to invite others to the group
    if(!(requesting_member->permissions & GRP_PERM_CAN_INVITE))
    {
        printf("User \"%s\" tried to invite users from group \"%s\", but the user does not have the permission.\n", current_client->username, group_name);
        send_msg(current_client, "NoPermission", 13);
        return 0;
    }

    //Invite each member specified in the command
    token = strtok(NULL, ",");
    while(token)
    {
        //Locate the member to be Added
        HASH_FIND_STR(active_users, token, target_user);
        if(!target_user)
        {
            printf("User \"%s\" was not found.\n", token);
            send_msg(current_client, "UserNotFound", 13);
            return 0;
        }

        //Send an invitation to the user
        if(invite_to_group_direct(group, target_user))
            ++users_invited;

        token = strtok(NULL, ",");
    }

    return users_invited;
}


int kick_from_group()
{
    char group_name[USERNAME_LENG+1];
    char kick_msg[BUFSIZE];
    char *newbuffer = buffer, *token;
    Group* group;
    Group_Member* target_member;
    Namelist *group_entry;

    token = strtok(newbuffer, ",");
    sscanf(token, "!kickgroup=%[^,]", group_name);

    if(!basic_group_permission_check(group_name, &group, &target_member))
        return 0;

    //Check if the requesting user has suffice permissions to kick others
    if(!(target_member->permissions & GRP_PERM_CAN_KICK))
    {
        printf("User \"%s\" tried to kick users from group \"%s\", but the user does not have the permission.\n", current_client->username, group_name);
        send_msg(current_client, "NoPermission", 13);
        return 0;
    }

    //Kick each member specified in the command
    token = strtok(NULL, ",");
    while(token)
    {
        //Locate the member to be kicked
        HASH_FIND_STR(group->members, token, target_member);
        if(!target_member)
        {
            printf("User \"%s\" was not found.\n", token);
            send_msg(current_client, "UserNotFound", 13);
            return 0;
        }

        //Remove the member from the group's member table
        HASH_DEL(group->members, target_member);
        --group->member_count;

        //Find the entry in the client's group_joined list and remove the entry
        group_entry = find_from_namelist(target_member->c->groups_joined, group_name);
        if(group_entry)
        {
            LL_DELETE(target_member->c->groups_joined, group_entry);
            free(group_entry);
        }
        else
            printf("Group entry was not found in client's descriptor\n");
        

        //Announce to other existing members that a new member was invited
        sprintf(kick_msg, "User \"%s\" has been kicked by \"%s\" in group \"%s\"", target_member->username, current_client->username, group->groupname);
        printf("%s\n", kick_msg);
        send_group(group, kick_msg, strlen(kick_msg)+1);

        //Send an invite to the requested user to join the group
        sprintf(kick_msg, "!groupkicked=%s,by=%s", group->groupname, current_client->username);
        send_msg(target_member->c, kick_msg, strlen(kick_msg)+1);
        free(target_member);

        token = strtok(NULL, ",");
    }

    //Delete this group if no members are remaining
    if(group->member_count == 0)
    {
        printf("Group \"%s\" is now empty. Deleting...\n", group->groupname);
        remove_group(group);
    }

    return 1;
}


//To be done
int set_member_permission(Group *group, User *user, int new_permissions)
{
    return 0;
}

int set_group_permission(Group *group, int new_permissions)
{
    return 0;
}


/******************************/
/*     Group File Sharing     */
/******************************/

//Also see the "Client-Group File Sharing" section in file_transfer_server.c

int group_filelist()
{
    char* filelist_msg;
    unsigned int msg_size = 0, printed = 0;

    char group_name[USERNAME_LENG+1];
    Group *group = NULL;
    Group_Member *target_member;

    unsigned int file_count;
    File_List *curr, *temp;

    sscanf(buffer, "!filelist=%s", group_name);

    if(!basic_group_permission_check(group_name, &group, &target_member))
        return 0;
    
    //Check if group allows file transfers and the user has such permission.
    if( !(group->group_flags & GRP_FLAG_ALLOW_XFER) || !(target_member->permissions & GRP_PERM_CAN_GETFILE) )
    {
        printf("User \"%s\" is not permitted to list files from group \"%s\"\n", target_member->username, group_name);
        send_msg(current_client, "NoPermission", 13);
        return 0;
    }

    file_count = HASH_COUNT(group->filelist);
    filelist_msg = malloc(file_count * (MAX_FILENAME+1 + 128));

    sprintf(filelist_msg, "!filelist=%d,group=%s%n", file_count, group_name, &msg_size);

    //Iterate through the list of available files and append them to the buffer one at a time
    HASH_ITER(hh, group->filelist, curr, temp)
    {
        sprintf(&filelist_msg[msg_size], ",[%u,%s,%zu,%s]%n", 
                curr->fileid, curr->filename, curr->filesize, curr->uploader, &printed);
        
        msg_size += printed;
    }

    send_new_long_msg(filelist_msg, msg_size+1);
    free(filelist_msg);

    return file_count;
}

int add_file_to_group(Group *group, char *uploader, char *filename, size_t filesize, unsigned int checksum, char *target_file)
{
    File_List *new_file = calloc(1, sizeof(File_List));
    char new_file_msg[MAX_MSG_LENG];

    strcpy(new_file->uploader, uploader);
    strcpy(new_file->filename, filename);
    strcpy(new_file->target_file, target_file);
    new_file->filesize = filesize;
    new_file->checksum = checksum;
    new_file->fileid = ++group->last_fileid;

    HASH_ADD_INT(group->filelist, fileid, new_file);

    //TODO: Expiry timer for files uploaded to groups

    sprintf(new_file_msg, "New file available for download: \"%s\" (fileid: %d, %zu bytes) uploaded by \"%s\".", 
                new_file->filename, new_file->fileid, new_file->filesize, new_file->uploader);

    send_group(group, new_file_msg, strlen(new_file_msg)+1);
    return new_file->fileid;
}

int remove_file_from_group(Group *group, char *filename)
{
    File_List *requested_file;

    return 1;
}