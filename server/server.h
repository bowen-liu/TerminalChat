#ifndef _SERVER_H_
#define _SERVER_H_

#include "../common/common.h"
#include "../library/uthash/uthash.h"                //http://troydhanson.github.io/uthash/       


//Abstracts each active client participating in the server
typedef struct {
    int socketfd;                        //Key used for the main active client hash table
    struct sockaddr_in sockaddr;
    int sockaddr_leng;

    char username[USERNAME_LENG];
    UT_hash_handle hh;
} Client;


//Maps a username to a client object
typedef struct {
    char username[USERNAME_LENG];
    Client *c;
    
    UT_hash_handle hh;
} Username_Map;


#endif