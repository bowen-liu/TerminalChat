#ifndef _GROUP_H_
#define _GROUP_H_

/*Permisison Flags*/
#define GRP_PERM_HAS_JOINED 0x1
#define GRP_PERM_CAN_TALK 0x2
#define GRP_PERM_CAN_INVITE 0x4
#define GRP_PERM_CAN_KICK 0x8
#define GRP_PERM_CAN_SETPERM 0x10

#define GRP_PERM_DEFAULT (GRP_PERM_CAN_TALK | GRP_PERM_CAN_INVITE)
#define GRP_PERM_ADMIN   (GRP_PERM_CAN_TALK | GRP_PERM_CAN_INVITE | GRP_PERM_CAN_KICK | GRP_PERM_CAN_SETPERM)


/*Data structures*/

//Should be castable to a User instead
typedef struct {
    char username[USERNAME_LENG+1];
    Client *c;
    UT_hash_handle hh;

    int permissions;
} Group_Member;

typedef struct group {
    char groupname[USERNAME_LENG+1];
    Group_Member *members;
    unsigned int member_count;
    //unsigned int invite_only : 1;

    UT_hash_handle hh;
} Group;


unsigned int send_group(Group* group, char* buffer, size_t size);
int group_msg();
void disconnect_client_group_cleanup(Client *c);

int userlist_group(char *group_name);
int create_new_group();
int leave_group_direct(Group *group, Client *c);
int leave_group();
int join_group();
int invite_to_group();
int kick_from_group();
int change_group_member_permission(Group *group, User *user, int new_permissions);

#endif