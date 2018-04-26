#include "server.h"

#define MAX_CONNECTION_BACKLOG 8
#define MAX_EPOLL_EVENTS    32 

//Server socket structures
static int server_socketfd;
static struct sockaddr_in server_addr;              //"struct sockaddr_in" can be casted as "struct sockaddr" for binding

//epoll FD for handling multiple clients 
static int epoll_fd;  

//Receive buffers
static char *buffer;
static size_t buffer_size = BUFSIZE;

//Keeping track of clients
Client *active_connections = NULL;                  //Hashtable of all active client sockets (key = socketfd)
User *active_users = NULL;                          //Hashtable of all active users (key = username), mapped to their client descriptors
Group* groups = NULL;                               //Hashtable of all user created private chatrooms (key = groupname)
unsigned int total_users = 0;    

//Client being served right now
Client *current_client;                         //Descriptor for the client being serviced right now


/******************************/
/*          Helpers           */
/******************************/                 

static unsigned int send_bcast(char* buffer, size_t size, int is_control_msg, int include_current_client);
static int leave_group_direct(Group *group, Client *c);

static void disconnect_client(Client *c)
{
    char disconnect_msg[BUFSIZE];
    User *User;

    Namelist *current_group_name, *tmp_name;
    Group *group;
    
    if(epoll_ctl(epoll_fd, EPOLL_CTL_DEL, c->socketfd, NULL) < 0) 
        perror("Failed to unregister the disconnected client from epoll!\n");

    close(c->socketfd);

    //Check if the connection is for a registered client
    if(strlen(c->username) == 0)
    {
        printf("Dropping unregistered connection...\n");
        HASH_DEL(active_connections, c);
        free(c);

        return;
    }

    //Leave participating chat groups. TODO: Complete implementing this!
    LL_FOREACH_SAFE(c->groups_joined, current_group_name, tmp_name)
    {
        printf("Leaving group \"%s\"\n", current_group_name->name);
        HASH_FIND_STR(groups, current_group_name->name, group);

        if(group)
        {
            leave_group_direct(group, current_client);
            LL_DELETE(current_client->groups_joined, current_group_name);
            free(current_group_name);
        }
    }
        
    //Free up resources used by the user
    sprintf(disconnect_msg, "!useroffline=%s", c->username);
    printf("Disconnecting \"%s\"\n", c->username);
    send_bcast(disconnect_msg, strlen(disconnect_msg)+1, 1, 0);

    HASH_FIND_STR(active_users, c->username, User);
    HASH_DEL(active_users, User);
    free(User);
    
    HASH_DEL(active_connections, c);
    free(c);

    --total_users;
}


/******************************/
/*     Basic Send/Receive     */
/******************************/

static unsigned int send_msg(Client *c, char* buffer, size_t size)
{
    int bytes;

    if(size == 0)
        return 0;
        
    else if(size > MAX_MSG_LENG)
    {
        printf("Cannot send this message. Size too big\n");
        return 0;
    }
    
    bytes = send(c->socketfd, buffer, size, 0);
    if(bytes < 0)
    {
        perror("Failed to send greeting message to client\n");
        disconnect_client(c);
        return 0;
    }
    return bytes;
}


static unsigned int send_bcast(char* buffer, size_t size, int is_control_msg, int include_current_client)
{
    int count = 0;
    char bcast_msg[BUFSIZE];
    Client *curr, *temp;

    if(is_control_msg)
        sprintf(bcast_msg, "%s", buffer);
    else
        sprintf(bcast_msg, "%s: %s", current_client->username, buffer);
    
    //Send the message in the buffer to every active client
    HASH_ITER(hh, active_connections, curr, temp)
    {
        if(!include_current_client && curr->socketfd == current_client->socketfd)
            continue;
        
        send_msg(curr, bcast_msg, strlen(bcast_msg)+1);
        ++count;
    }
    
    return count;
}


static void send_long_msg()
{
    int remaining_size = current_client->pending_size - current_client->pending_processed;
    int bytes;

    //Start of a new long send. Register the client's FD to signal on EPOLLOUT
    if(current_client->pending_processed == 0)
        update_epoll_events(epoll_fd, current_client->socketfd, CLIENT_EPOLL_DEFAULT_EVENTS | EPOLLOUT);
     
    //Send the next chunk to the client
    bytes = send(current_client->socketfd, &current_client->pending_buffer[current_client->pending_processed], (remaining_size >= LONG_RECV_PAGE_SIZE)? LONG_RECV_PAGE_SIZE:remaining_size, 0);
    printf("Sending chunk (%d): %.*s\n", bytes, bytes, &current_client->pending_buffer[current_client->pending_processed]);

    //Check exit conditions
    if(bytes <= 0)
    {
        perror("Failed to send long message\n");
        printf("Sent %zu\\%zu bytes to client \"%s\" before failure.\n", current_client->pending_processed, current_client->pending_size, current_client->username);
    }
    else
    {
        current_client->pending_processed += bytes;
        printf("Sent %zu\\%zu bytes to client \"%s\"\n", current_client->pending_processed, current_client->pending_size, current_client->username);

        if(current_client->pending_processed < current_client->pending_size)
            return;
    }

    //Cleanup resources once long sent has completed/failed
    free(current_client->pending_buffer);
    current_client->pending_buffer = NULL;
    current_client->pending_size = 0;
    current_client->pending_processed = 0;

    //Remove the EPOLLOUT notification once long send has completed
    update_epoll_events(epoll_fd, current_client->socketfd, CLIENT_EPOLL_DEFAULT_EVENTS);
}


static void send_new_long_msg(char* buffer, size_t size)
{
    char* newbuffer;
    size_t header_size;

    //if(size < BUFSIZE)
    if(size < LONG_RECV_PAGE_SIZE)
    {
        send_msg(current_client, buffer, size);
        return;
    }

    newbuffer = malloc(size + 48);

    //Add the longmsg header and total message size before the message
    sprintf(newbuffer, "!longmsg=%zu ", size);
    header_size = strlen(newbuffer);
    strcat(newbuffer, buffer);

    printf("New Long Message (h: %zu m: %zu): \"%s\"\n", header_size, size, newbuffer);

    //Start the operation
    current_client->pending_buffer = newbuffer;
    current_client->pending_size = size + header_size;
    current_client->pending_processed = 0;
    send_long_msg();
}


static unsigned int recv_msg(Client *c, char* buffer, size_t size)
{
    int bytes = recv(c->socketfd, buffer, BUFSIZE, 0);
    if(bytes < 0)
    {
        perror("Failed to receive from client. Disconnecting...\n");
        disconnect_client(c);
        return 0;
    }

    //Client has disconnected unexpectedly
    else if(bytes == 0)
    {
        printf("Unexpected disconnect from client %s:%d : \n", inet_ntoa(c->sockaddr.sin_addr), ntohs(c->sockaddr.sin_port));
        disconnect_client(c);
        return 0;
    }

    return bytes;
}



/******************************/
/*      Group Operations    */
/******************************/

static unsigned int send_group(Group* group, char* buffer, size_t size)
{
    unsigned int members_sent = 0;
    User *curr = NULL, *tmp;

    HASH_ITER(hh, group->members, curr, tmp) 
    {
        if(!send_msg(curr->c, buffer, size))
            printf("Send failed to user \"%s\" in group \"%s\"\n", curr->username, group->groupname);
        else
            ++members_sent;
    }

    return members_sent;
}

static inline int group_msg()
{
    char *target_group;
    char *target_msg;
    char gmsg[MAX_MSG_LENG];
    Group *target;
    User *sender_is_member;

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

    //Checks if t he sender is part of the group
    HASH_FIND_STR(target->members, current_client->username, sender_is_member);
    if(!sender_is_member)
    {
        printf("User \"%s\" is not a member of \"%s\"\n", current_client->username, target_group);
        send_msg(current_client, "NoPermission", 13);
        return 0;
    }

    //Forward message to the target
    sprintf(gmsg, "%s (%s): %s", current_client->username, target->groupname, target_msg);
    return send_group(target, gmsg, strlen(gmsg)+1);
}

//!newgroup=eeee,abc,def

static void join_group_direct(Group *group, Client *c);
static inline int create_new_group()
{
    Group* newgroup = calloc(1, sizeof(Group));
    Group* groupname_exists = NULL;

    User *target_user = NULL, *newmember;
    Namelist *newgroup_entry;

    char *newbuffer = buffer, *token;
    int retval;

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
    

    //Add each specified member into the group, starting with the group creator
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

        join_group_direct(newgroup, target_user->c);

        token = strtok(NULL, ",");
    }

    
    //Tell each of the clients to join the group
    sprintf(buffer, "!groupinvite=%s,sender=%s", newgroup->groupname, current_client->username);
    retval = send_group(newgroup, buffer, strlen(buffer)+1);

    if(!retval)
    {
        printf("Unable to invite any members to the new group. Cancelling...\n");
        free(newgroup);
    }

    return retval;
}


static int leave_group_direct(Group *group, Client *c)
{
    char leavemsg[BUFSIZE];
    User* target_user;
    
    HASH_FIND_STR(group->members, c->username, target_user);
    if(!target_user)
    {
        printf("Leave: User \"%s\" not found in group \"%s\".\n", c->username, group->groupname);
        return 0;
    }

    HASH_DEL(group->members, target_user);
    --group->member_count;
    free(target_user);

    sprintf(leavemsg, "!leftgroup=%s,user=%s", group->groupname, c->username);
    printf("User \"%s\" has left the group \"%s\".\n", c->username, group->groupname);
    send_group(group, leavemsg, strlen(leavemsg)+1);

    return 1;
}

static int leave_group()
{
    char groupname[USERNAME_LENG+1];
    Namelist *current_group_name, *tmp_name;
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
    LL_FOREACH_SAFE(current_client->groups_joined, current_group_name, tmp_name)
    {
        if(strcmp(groupname, current_group_name->name) == 0)
            break;
        else 
            current_group_name = NULL;
    }

    if(!current_group_name)
    {
        printf("Leave: User \"%s\" not found in group \"%s\" (by joined_groups).\n", current_client->username, groupname);
        send_msg(current_client, "NoPermission", 13);
        return 0;
    }

    LL_DELETE(current_client->groups_joined, current_group_name);
    free(current_group_name);

    //Leave the group officially
    return leave_group_direct(group, current_client);
}


static void join_group_direct(Group *group, Client *c)
{
    User *newmember = malloc(sizeof(User));
    Namelist *newgroup_entry;

    strcpy(newmember->username, c->username);
    newmember->c = c;
    HASH_ADD_STR(group->members, c->username, newmember);
    ++group->member_count;
    
    //Record the participation of this group for each user's client descriptors
    newgroup_entry = calloc(1, sizeof(Namelist));
    strcpy(newgroup_entry->name, group->groupname);
    LL_APPEND(c->groups_joined, newgroup_entry);

    printf("Added member \"%s\" to group \"%s\"\n", c->username, group->groupname);

    //Now you must send a !groupinvite command to the new member
}


static int join_group()
{
    Namelist *new_group_entry = calloc(1, sizeof(Namelist));
    Group *group;
    
    sscanf(buffer, "!joingroup=%s", new_group_entry->name);

    //Locate this group in the hash table
    HASH_FIND_STR(groups, new_group_entry->name, group);
    if(!group)
    {
        printf("Group \"%s\" was not found.\n", new_group_entry->name);
        send_msg(current_client, "InvalidGroup", 13);
        return 0;
    }

    //Add an entry to the client's groups_joined list
    join_group_direct(group, current_client);

    //Inform the new member that the join was sucessful
    sprintf(buffer, "!groupjoined=%s", group->groupname);
    if(!send_msg(current_client, buffer, strlen(buffer)+1))       //TODO: what if send failed?
        return 0;

    //Announce to other existing members that a new member has joined the group
    sprintf(buffer, "!joinedgroup=%s,user=%s", group->groupname, current_client->username);
    send_group(group, buffer, strlen(buffer)+1);

    return 1;
}

static int invite_to_group()
{
    char username[USERNAME_LENG+1];
    Namelist *new_group_entry = calloc(1, sizeof(Namelist));
    Group* group;
    User* target_user;
    
    sscanf(buffer, "!invitegroup=%[^,],user=%s", new_group_entry->name, username);

    //Locate this group in the hash table
    HASH_FIND_STR(groups, new_group_entry->name, group);
    if(!group)
    {
        printf("Group \"%s\" was not found.\n", new_group_entry->name);
        send_msg(current_client, "InvalidGroup", 13);
        free(new_group_entry);
        return 0;
    }

    //Check if this user is already in the group
    HASH_FIND_STR(group->members, username, target_user);
    if(target_user)
    {
        printf("User \"%s\" is already a member of the group \"%s\".\n", username, new_group_entry->name);
        send_msg(current_client, "AlreadyExist", 13);
        free(new_group_entry);
        return 0;
    }

    //Locate the member to be Added
    HASH_FIND_STR(active_users, username, target_user);
    if(!target_user)
    {
        printf("User \"%s\" was not found.\n", username);
        send_msg(current_client, "UserNotFound", 13);
        free(new_group_entry);
        return 0;
    }

    //Announce to other existing members that a new member was invited
    sprintf(buffer, "User \"%s\" has invited \"%s\" to join the group \"%s\"", current_client->username, username, group->groupname);
    send_group(group, buffer, strlen(buffer)+1);

    //Add an entry to the client's groups_joined list
    join_group_direct(group, target_user->c);

    //Invite the requested member to join the group
    sprintf(buffer, "!groupinvite=%s,sender=%s", new_group_entry->name, current_client->username);
    if(!send_msg(target_user->c, buffer, strlen(buffer)+1))       //TODO: what if send failed?
        return 0;

    return 1;
}


/******************************/
/*      Client Operations    */
/******************************/

static inline int client_pm()
{
    char *target_username;
    char *target_msg;
    char pmsg[MAX_MSG_LENG];
    User *target;

    //Find the occurance of the first space
    target_msg = strchr(buffer, ' ');
    if(!target_msg)
    {
        printf("Invalid username specified, or no message specified\n");
        return 0;
    }

    //Seperate the target's name and message from the buffer
    target_username = &buffer[1];
    target_msg[0] = '\0';              //Mark the space separating the username and message as NULL, so we can directly read the username from the pointer
    target_msg += sizeof(char);        //Increment the message pointer by 1 to skip to the actual message

    //Find if anyone with the requested username is online
    HASH_FIND_STR(active_users, target_username, target);
    if(!target)
    {
        printf("User \"%s\" not found\n", target_username);
        send_msg(current_client, "UserNotFound", 13);
        return 0;
    }

    //Forward message to the target
    sprintf(pmsg, "%s (PM): %s", current_client->username, target_msg);
    if(!send_msg(target->c, pmsg, strlen(pmsg)+1))
        return 0;

    //Echo back to the sender
    sprintf(pmsg, "%s (PM to %s): %s", current_client->username, target_username, target_msg);
    if(!send_msg(current_client, pmsg, strlen(pmsg)+1))
        return 0;
    
    return 1;
}

static inline int register_client()
{
    char *username = malloc(USERNAME_LENG+1);
    User *result;

    char *new_username;
    int duplicates = 0, max_duplicates_allowed;

    sscanf(buffer, "!register:username=%s", username);
    if(!name_is_valid(username))
        return 0;
    
    //Check if the requested username is a duplicate. Append a number (up to 999) after the username if it already exists 
    HASH_FIND_STR(active_users, username, result);
    while(result != NULL)
    {
        if(duplicates == 0)
        {
            new_username = malloc(USERNAME_LENG+1);

            //How many reminaing free bytes in the username can be used for appending numbers?
            max_duplicates_allowed = USERNAME_LENG - strlen(username) - 1;

            //Determine the largest numerical value (up to 999) can be used from the free bytes
            if(max_duplicates_allowed > 3)
                max_duplicates_allowed = 999;
            else if(max_duplicates_allowed == 2)
                max_duplicates_allowed = 99;
            else if(max_duplicates_allowed == 1)
                max_duplicates_allowed = 9;
            else
                max_duplicates_allowed = 0;
        }

        if(++duplicates > max_duplicates_allowed)
        {
            printf("The username \"%s\" cannot support further clients.\n", username);
            send_msg(current_client, "InvalidUsername", 16);
            disconnect_client(current_client);
            return 0;
        }

        //Test if the new name with a newly appended value is used
        sprintf(new_username, "%s_%d", username, duplicates);
        HASH_FIND_STR(active_users, new_username, result);
    }

    if(duplicates)
    {
        printf("Found %d other clients with the same username. Changed username to \"%s\".\n", duplicates, new_username);
        free(username);
        username = new_username;
    }

    //Register the client's requested username
    result = malloc(sizeof(User));
    result->c = current_client;
    strcpy(current_client->username, username);
    strcpy(result->username, username);
    HASH_ADD_STR(active_users, username, result);
    free(username);

    printf("Registering user \"%s\"\n", current_client->username);
    sprintf(buffer, "!regreply:username=%s", current_client->username);

    if(!send_msg(current_client, buffer, strlen(buffer)+1))
        return 0;
    
    sprintf(buffer, "!useronline=%s", current_client->username);
    send_bcast(buffer, strlen(buffer)+1, 1, 0);
    ++total_users;
    
    return 1;
}


static inline int userlist()
{
    char* userlist_msg = malloc(total_users * (USERNAME_LENG+1) + EXTRA_CHARS);
    size_t userlist_size = 0;
    User *curr, *temp;

    //Add command header to the beginning of the buffer
    sprintf(userlist_msg, "!userlist=%d", total_users);
    userlist_size = strlen(userlist_msg);

    //Iterate through the list of active usernames and append them to the buffer one at a time
    HASH_ITER(hh, active_users, curr, temp)
    {
        strcat(userlist_msg, ",");
        strcat(userlist_msg, curr->username);
        userlist_size += strlen(curr->username) + 1;
    }
    userlist_msg[userlist_size] = '\0';
    ++userlist_size;
    
    send_new_long_msg(userlist_msg, userlist_size);
    free(userlist_msg);

    return 1;
}

static inline int parse_client_command()
{
    int bytes; 

    //If the client requested to close the connection
    if(strncmp(buffer, "!register:", 10) == 0)
    {  
        return register_client();
    }

    else if(strcmp(buffer, "!close") == 0)
    {
        printf("Closing connection with client %s on port %d\n", inet_ntoa(current_client->sockaddr.sin_addr), ntohs(current_client->sockaddr.sin_port));
        disconnect_client(current_client);
        return -1;
    }

    else if(strcmp(buffer, "!userlist") == 0)
    {
        return userlist();
    }

    else if(strcmp(buffer, "!testlong") == 0)
    {
        char testmsg[101] = "1111111111222222222233333333334444444444555555555566666666667777777777888888888899999999990000000000";
        size_t testmsg_size = strlen(testmsg)+1;
        
        send_new_long_msg(testmsg, testmsg_size);
        return 1;
    }

    else if(strncmp(buffer, "!newgroup=", 10) == 0)
    {
        return create_new_group();
    }

    else if(strncmp(buffer, "!joingroup=", 11) == 0)
    {
        return join_group();
    }

    else if(strncmp(buffer, "!leavegroup=", 12) == 0)
    {
        return leave_group();
    }

    else if(strncmp(buffer, "!invitegroup=", 13) == 0)
    {
        return invite_to_group();
    }

    else
    {
        printf("Invalid command \"%s\"\n", buffer);
        send_msg(current_client, "InvalidCmd.", 12);
        return 0;
    }
        
    return 1;
}


/******************************/
/*    Serverside Operations   */
/******************************/


static inline int handle_client_msg()
{
    int bytes, retval;
    
    bytes = recv_msg(current_client, buffer, BUFSIZE);
    if(!bytes)
        return 0;
    
    printf("Received %d bytes from %s:%d : ", bytes, inet_ntoa(current_client->sockaddr.sin_addr), ntohs(current_client->sockaddr.sin_port));
    printf("%s\n", buffer);

    //Parse as a command if message begins with '!'
    if(buffer[0] == '!')
        return parse_client_command();

    //Private messaging between two users if message starts with "@". Group message if message starts with "@@"
    else if(buffer[0] == '@')
    {
        if(buffer[1] == '@')
            return group_msg();
        else
            return client_pm();
    }

    //Broadcast regular messages to all other active clients
    else
        send_bcast(buffer, buffer_size, 0, 1);
        
    return 1;
}


static inline int handle_new_connection()
{    
    Client *new_client = calloc(1, sizeof(Client));
    current_client = new_client;
    
    new_client->username[0] = '\0';                             //Empty username indicates an unregistered connection
    new_client->sockaddr_leng = sizeof(struct sockaddr_in);
    new_client->socketfd = accept(server_socketfd, (struct sockaddr*) &new_client->sockaddr, &new_client->sockaddr_leng);

    if(new_client->socketfd < 0)
    {
        perror("Error accepting client!\n");
        free(new_client);
        return 0;
    }
    printf("Accepted client %s:%d\n", inet_ntoa(new_client->sockaddr.sin_addr), ntohs(new_client->sockaddr.sin_port));

    //Add the client into active_connections, and use its socketfd as the key.
    HASH_ADD_INT(active_connections, socketfd, new_client);

    //Register the new client's FD into epoll's event list (edge triggered), and mark it as nonblocking
    fcntl(new_client->socketfd, F_SETFL, O_NONBLOCK);
    if(!register_fd_with_epoll(epoll_fd, new_client->socketfd, CLIENT_EPOLL_DEFAULT_EVENTS))
        return 0;    

    //Send a greeting to the client
    if(!send_msg(new_client, "Hello World!", 13))
        return 0;
    
    return 1;
} 


static inline int handle_stdin()
{
    int bytes; 

    //Read from stdin and remove the newline character
    bytes = getline(&buffer, &buffer_size, stdin);
    if (buffer[bytes-1] == '\n') 
    {
        buffer[bytes-1] = '\0';
        --bytes;
    }
    
    printf("stdin: %s\n", buffer);

    return bytes;
}





/******************************/
/*     Server Entry Point     */
/******************************/

static inline void server_main_loop()
{
    struct epoll_event events[MAX_EPOLL_EVENTS];
    int ready_count, i;
    
    while(1)
    {
        //Wait until epoll has detected some event in the registered fd's
        ready_count = epoll_wait(epoll_fd, events, MAX_EPOLL_EVENTS, -1);
        if(ready_count < 0)
        {
            perror("epoll_wait failed!\n");
            return;
        }


        for(i=0; i<ready_count; i++)
        {
            //When the administrator enters a command from stdin
            if(events[i].data.fd == 0)
            {
                handle_stdin();
            }
            
            //When a new connection arrives to the server socket, accept it
            else if(events[i].data.fd == server_socketfd)
            {
                handle_new_connection();
            }

            //When an event is occuring on an existing client connection
            else
            {                
                HASH_FIND_INT(active_connections, &events[i].data.fd, current_client);

                //Handle EPOLLRDHUP: the client has closed its connection unexpectedly
                if(events[i].events & EPOLLRDHUP)
                {
                    printf("Client \"%s\" has closed its connection.\n", current_client->username);
                    disconnect_client(current_client);
                    continue;
                }
                
                //Handles EPOLLIN (ready for reading)
                else if(events[i].events & EPOLLIN)
                    handle_client_msg();

                //Handle EPOLLOUT (ready for writing) if the client has pending long messages
                else if(events[i].events & EPOLLOUT)
                {
                    if(current_client->pending_size > 0)
                        send_long_msg();
                }
            }
                
        }
    }
}



void server(const char* ipaddr, const int port)
{   
    /*Initialize network buffer*/
    buffer = calloc(BUFSIZE, sizeof(char));

    /*Setup epoll to allow multiplexed IO to serve multiple clients*/
    epoll_fd = epoll_create1(0);
    if(epoll_fd < 0)
    {
        perror("Failed to create epoll!\n");
        return;
    }
    
    /*Create a TCP server socket*/
    server_socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_socketfd < 0)
    {
        perror("Error creating socket!\n");
        return;
    }

    /*Bind specified IP/Port to the socket*/
    memset(&server_addr, 0, sizeof(struct sockaddr_in));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    //server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_addr.s_addr = inet_addr(ipaddr);

    if(bind(server_socketfd, (struct sockaddr*) &server_addr, sizeof(struct sockaddr)) < 0)
    {
        perror("Failed to bind socket!\n");
        return;
    }


    /*Register the server socket to the epoll list, and also mark it as nonblocking*/
    fcntl(server_socketfd, F_SETFL, O_NONBLOCK);
    if(!register_fd_with_epoll(epoll_fd, server_socketfd, EPOLLIN))
            return;   
    
    /*Register stdin (fd = 0) to the epoll list*/
    if(!register_fd_with_epoll(epoll_fd, 0, EPOLLIN))
        return;   

    /*Begin listening for incoming connections on the server socket*/
    if(listen(server_socketfd, MAX_CONNECTION_BACKLOG) < 0)
    {
        perror("Failed to listen to the socket!\n");
        return;
    }

    /*Begin handling requests*/
    server_main_loop();
}