#ifndef _SERVER_H_
#define _SERVER_H_

#include "../common/common.h"
#include "../library/uthash/uthash.h"                //http://troydhanson.github.io/uthash/       


//Abstracts each active client participating in the server
typedef struct {
    
    /*Connection info*/
    int socketfd;                        //Key used for the main active client hash table
    struct sockaddr_in sockaddr;
    int sockaddr_leng;

    /*Userinfo*/
    char username[USERNAME_LENG];

    /*Other data*/
    char* pending_buffer;
    size_t pending_size;
    size_t pending_processed;


    UT_hash_handle hh;
} Client;


//Maps a username to a client object
typedef struct {
    char username[USERNAME_LENG];
    Client *c;
    
    UT_hash_handle hh;
} Username_Map;


#define CLIENT_EPOLL_DEFAULT_EVENTS (EPOLLIN | EPOLLRDHUP)

#endif