#include "server_common.h"
#include "server.h"
#include "commands.h"



/******************************/
/*      Client Operations     */
/******************************/

int parse_client_command()
{
    /*Connection related commands*/

    if(strcmp(msg_body, "!close") == 0)
    {
        printf("Closing connection with client %s on port %d\n", inet_ntoa(current_client->sockaddr.sin_addr), ntohs(current_client->sockaddr.sin_port));
        disconnect_client(current_client, NULL);
        return -1;
    }


    /*Group related Commands. Implemented in group.c*/
    else if(strcmp(msg_body, "!grouplist") == 0)
        return grouplist();
    
    else if(strncmp(msg_body, "!userlist", 9) == 0)
        return userlist();

    else if(strncmp(msg_body, "!newgroup ", 10) == 0)
        return create_new_group();

    else if(strncmp(msg_body, "!join ", 6) == 0)
        return join_group();

    else if(strncmp(msg_body, "!leave ", 7) == 0)
        return leave_group();

    else if(strncmp(msg_body, "!invite ", 8) == 0)
        return invite_to_group();

    else if(strncmp(msg_body, "!kick ", 6) == 0)
        return kick_from_group();

    else if(strncmp(msg_body, "!ban ", 5) == 0)
        return ban_from_group();

    else if(strncmp(msg_body, "!unban ", 7) == 0)
        return unban_from_group();

    else if(strncmp(msg_body, "!setperm ", 9) == 0)
        return set_member_permission();
    
    else if(strncmp(msg_body, "!setflag ", 9) == 0)
        return set_group_permission();


    /*File Transfer Commands. Implemented in file_transfer_server.c*/
    else if(strncmp(msg_body, "!sendfile=", 10) == 0)
        return new_client_transfer();

    else if(strncmp(msg_body, "!acceptfile=", 12) == 0)
        return accepted_file_transfer();

    else if(strncmp(msg_body, "!rejectfile=", 12) == 0)
        return rejected_file_transfer();

    else if(strncmp(msg_body, "!cancelfile", 11) == 0)
        return user_cancelled_transfer();

    /*File Transfer for Groups*/
    else if(strcmp(msg_body, "!filelist") == 0)
        return group_filelist();

    else if(strncmp(msg_body, "!putfile=", 9) == 0)
        return put_new_file_to_group();

    else if(strncmp(msg_body, "!getfile ", 9) == 0)
        return get_new_file_from_group();

    else if(strncmp(msg_body, "!removefile ", 12) == 0)
        return remove_file_from_group();
    
    
    else
    {
        printf("Invalid command \"%s\"\n", msg_body);
        send_error_code(current_client, ERR_INVALID_CMD, NULL);
        return 0;
    }
        
    return 1;
}



/******************************/
/*   Server Admin Operations  */
/******************************/

static void admin_delete_group(char *buffer)
{
    char groupname[USERNAME_LENG+1], *groupname_plain;
    Group *group;

    sscanf(buffer, "!delgroup %s", groupname);
    groupname_plain = plain_name(groupname);

    HASH_FIND_STR(groups, groupname_plain, group);
    if(!group)
    {
        printf("Group \"%s\" was not found.\n", groupname_plain);
        return;
    }

    remove_group(group);
}


static void admin_unban_user(char *buffer)
{
    char unban_target[USERNAME_LENG+1];
    IP_List *ban_entry;
    char ipaddr_str[INET_ADDRSTRLEN];
    uint32_t target_ipaddr;

    sscanf(buffer, "!unbanip %s", unban_target);

    //We only allow banned hostname/IP addresses to be entered  
    if(hostname_to_ip(unban_target, "0", ipaddr_str))
        target_ipaddr = inet_addr(ipaddr_str);
    else
    {
        printf("Target \"%s\" was not found or not banned.\n", unban_target);
        return;
    }

    //Unban the IP address, if an entry was located
    HASH_FIND_INT(banned_ips, &target_ipaddr, ban_entry);
    if(ban_entry)
    {
        HASH_DEL(banned_ips, ban_entry);
        free(ban_entry);
        printf("Target \"%s\"has been unbanned.\n", unban_target);
    }
    else
        printf("Target \"%s\" was not found or not banned.\n", unban_target);
}

static void admin_ban_user(char *buffer)
{
    char target_name[USERNAME_LENG+1], *target_name_plain;
    User *target_user;
    Client *curr, *tmp;

    char ipaddr_str[INET_ADDRSTRLEN];
    uint32_t target_ipaddr;
    IP_List *ban_entry;

    sscanf(buffer, "!banip %s", target_name);
    target_name_plain = plain_name(target_name);
    
    //Locate the member to be banned
    HASH_FIND_STR(active_users, target_name_plain, target_user);
    if(target_user)
    {
        target_ipaddr = target_user->c->sockaddr.sin_addr.s_addr;
        strcpy(ipaddr_str, inet_ntoa(target_user->c->sockaddr.sin_addr));
    }

    //We also allow IP/hostnames to be directly specified
    else if(hostname_to_ip(target_name, "0", ipaddr_str))
        target_ipaddr = inet_addr(ipaddr_str);
    
    else
    {
        printf("Target \"%s\" was not found.\n", target_name);
        return;
    }

    //Add the user's associated IP address to the global ban list if it doesn't already exist
    HASH_FIND_INT(banned_ips, &target_ipaddr, ban_entry);
    if(!ban_entry)
    {
        ban_entry = calloc(1, sizeof(IP_List));
        ban_entry->ipaddr = target_ipaddr;
        HASH_ADD_INT(banned_ips, ipaddr, ban_entry);
    }
    printf("Target \"%s\" has been IP banned (%s).\n", target_name, ipaddr_str);

    //Drop every user currently connect with the banned IP address
    HASH_ITER(hh, active_connections, curr, tmp)
    {
        if(curr->sockaddr.sin_addr.s_addr == target_ipaddr)
            disconnect_client(curr, "IP Banned");
    }
}

static void admin_drop_user(char *buffer)
{
    char *kick_msg;
    char target_name[USERNAME_LENG+1], *target_name_plain;
    User *target_user;

    sscanf(buffer, "!dropuser %s", target_name);
    target_name_plain = plain_name(target_name);

    HASH_FIND_STR(active_users, target_name_plain, target_user);
    if(!target_user)
    {
        printf("User \"%s\" was not found.\n", target_name_plain);
        return;
    }

    //Tell the user about the ban
    kick_msg = malloc(BUFSIZE);
    sprintf(kick_msg, "User \"%s\" has been dropped from the server.", target_user->username);
    printf("%s\n", kick_msg);
    send_msg(target_user->c, kick_msg, strlen(kick_msg)+1);
    free(kick_msg);

    disconnect_client(target_user->c, "Dropped by Admin");
}

static void admin_promote_user(char *buffer)
{
    User *target_user;
    GroupList *curr, *tmp;
    
    char target_name[USERNAME_LENG+1], *target_name_plain;
    char *promote_msg = "\n***YOU ARE NOW A SERVER ADMIN***\nPlease be responsible with your actions...\n\n";

    sscanf(buffer, "!promoteuser %s", target_name);
    target_name_plain = plain_name(target_name);

    HASH_FIND_STR(active_users, target_name_plain, target_user);
    if(!target_user)
    {
        printf("User \"%s\" was not found.\n", target_name_plain);
        return;
    }

    target_user->c->is_admin = 1;
    printf("User \"%s\" has been made into a server admin.\n", target_user->username);
    send_msg(target_user->c, promote_msg, strlen(promote_msg)+1);
}

static void admin_demote_user(char *buffer)
{
    char target_name[USERNAME_LENG+1], *target_name_plain;
    char *promote_msg = "\n***YOU HAVE BEEN DEMOTED TO A REGULAR USER***\n\n";
    User *target_user;

    sscanf(buffer, "!demoteuser %s", target_name);
    target_name_plain = plain_name(target_name);

    HASH_FIND_STR(active_users, target_name_plain, target_user);
    if(!target_user)
    {
        printf("User \"%s\" was not found.\n", target_name_plain);
        return;
    }

    target_user->c->is_admin = 0;
    printf("User \"%s\" has been demoted back to a regular user.\n", target_user->username);
    send_msg(target_user->c, promote_msg, strlen(promote_msg)+1);
}


int handle_admin_commands(char *buffer)
{
    char *new_msg;
    
    if(strcmp(buffer, "!shutdown") == 0)
        exit(0);

    else if(strncmp(buffer, "!bcast ", 7) == 0)
        send_bcast(&buffer[7], strlen(&buffer[7])+1);

    else if(strncmp(buffer, "!delgroup ", 10) == 0)
        admin_delete_group(buffer);

    else if(strncmp(buffer, "!dropuser ", 10) == 0)
        admin_drop_user(buffer);

    else if(strncmp(buffer, "!banip ", 7) == 0)
        admin_ban_user(buffer);
    
    else if(strncmp(buffer, "!unbanip ", 9) == 0)
        admin_unban_user(buffer);

    else if(strncmp(buffer, "!promoteuser ", 13) == 0)
        admin_promote_user(buffer);

    else if(strncmp(buffer, "!demoteuser ", 12) == 0)
        admin_demote_user(buffer);

    else
    {
        if(buffer[0] == '!')
        {
            printf("Invalid admin command.\n");
            return 0;
        }

        new_msg = malloc(BUFSIZE);
        sprintf(new_msg, "***admin*** (%s): %s", lobby->groupname, buffer);
        printf("%s\n", new_msg);
        send_group(lobby, new_msg, strlen(new_msg)+1);
        free(new_msg);
    }

    return 1;
}