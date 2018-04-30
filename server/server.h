#ifndef _SERVER_H_
#define _SERVER_H_

#include "../common/common.h"     

struct namelist;

//Abstracts each active client participating in the server
typedef struct {
    
    /*Connection info*/
    int socketfd;                        //Key used for the main active client hash table
    struct sockaddr_in sockaddr;
    int sockaddr_leng;

    /*Userinfo*/
    char username[USERNAME_LENG+1];

    /*Other data*/
    char* pending_buffer;
    size_t pending_size;
    size_t pending_processed;

    /**/
    struct namelist *groups_joined;

    UT_hash_handle hh;
} Client;


//Maps a username to a client object
typedef struct {
    char username[USERNAME_LENG+1];
    Client *c;
    UT_hash_handle hh;
} User;




/*Groups*/

//Permisison Flags
#define GRP_PERM_HAS_JOINED 0x1
#define GRP_PERM_CAN_TALK 0x2
#define GRP_PERM_CAN_INVITE 0x4
#define GRP_PERM_CAN_KICK 0x8
#define GRP_PERM_CAN_SETPERM 0x10

#define GRP_PERM_DEFAULT (GRP_PERM_CAN_TALK | GRP_PERM_CAN_INVITE)
#define GRP_PERM_ADMIN   (GRP_PERM_CAN_TALK | GRP_PERM_CAN_INVITE | GRP_PERM_CAN_KICK | GRP_PERM_CAN_SETPERM)



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










#define CLIENT_EPOLL_DEFAULT_EVENTS (EPOLLIN | EPOLLRDHUP)

#endif