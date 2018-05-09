#ifndef _SERVER_COMMON_H_
#define _SERVER_COMMON_H_

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
    int is_admin :1;

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



#endif