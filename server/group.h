#ifndef _GROUP_H_
#define _GROUP_H_

/*Permisison flags for group members*/
#define GRP_PERM_HAS_JOINED     0x1
#define GRP_PERM_CAN_TALK       0x2
#define GRP_PERM_CAN_INVITE     0x4
#define GRP_PERM_CAN_PUTFILE    0x8
#define GRP_PERM_CAN_GETFILE    0x10
#define GRP_PERM_CAN_KICK       0x20
#define GRP_PERM_CAN_SETPERM    0x40

#define GRP_PERM_DEFAULT        (GRP_PERM_CAN_TALK | GRP_PERM_CAN_INVITE | GRP_PERM_CAN_PUTFILE | GRP_PERM_CAN_GETFILE)
#define GRP_PERM_DEFAULT_ADMIN  (GRP_PERM_CAN_TALK | GRP_PERM_CAN_INVITE | GRP_PERM_CAN_PUTFILE | GRP_PERM_CAN_GETFILE | GRP_PERM_CAN_KICK | GRP_PERM_CAN_SETPERM)
#define GRP_PERM_ADMIN_CHECK    (GRP_PERM_CAN_KICK | GRP_PERM_CAN_SETPERM)

/*Permission flags for individual groups*/
#define GRP_FLAG_PERSISTENT     0x1
#define GRP_FLAG_INVITE_ONLY    0x2
#define GRP_FLAG_ALLOW_XFER     0x4

#define GRP_FLAG_DEFAULT        (GRP_FLAG_ALLOW_XFER)

/*Lobby Settings*/
#define LOBBY_FLAGS             (GRP_FLAG_PERSISTENT)
#define LOBBY_USER_PERM         (GRP_PERM_CAN_TALK)



/*Data structures*/

typedef struct {

    unsigned int fileid;
    char uploader[USERNAME_LENG+1];
    char filename[MAX_FILENAME+1];
    size_t filesize;
    unsigned int checksum;
    char target_file[MAX_FILE_PATH+1];

    UT_hash_handle hh;

} File_List;


typedef struct {
    char username[USERNAME_LENG+1];
    Client *c;
    UT_hash_handle hh;

    int permissions;
} Group_Member;


typedef struct group {
    char groupname[USERNAME_LENG+1];
    Group_Member *members;
    int group_flags;
    int default_user_permissions;

    //Banned IPs from joining this group
    IP_List *banned_ips;
    
    //For group file sharing
    unsigned int last_fileid;
    File_List *filelist;

    UT_hash_handle hh;
} Group;



extern Group *groups;                   
extern Group *lobby;  



void create_lobby_group();

unsigned int send_lobby(Client *c, char* buffer, size_t size);
unsigned int send_group(Group* group, char* buffer, size_t size);
int group_msg();
void disconnect_client_group_cleanup(Client *c);
int basic_group_permission_check(char *group_name, Group **group_ret, Group_Member **member_ret);
Group_Member* allocate_group_member(Group *group, Client *target_user, int permissions);

int userlist_group(char *group_name);
int create_new_group();
int leave_group_direct(Group *group, Client *c, char *reason);
int leave_group();
int join_group();
int invite_to_group();
int kick_from_group();
int ban_from_group();
int unban_from_group();
int set_member_permission();
int set_group_permission();

int group_filelist();
int add_file_to_group(Group *group, char *uploader, char *filename, size_t filesize, unsigned int checksum, char *target_file);
int remove_file_from_group(Group *group, char *filename);

#endif