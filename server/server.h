#ifndef _SERVER_H_
#define _SERVER_H_

#include "../common/common.h"
#include "../library/uthash/uthash.h"                //http://troydhanson.github.io/uthash/       

typedef struct {
    int socketfd;                        //Key for the hashtable entry
    struct sockaddr_in sockaddr;
    int sockaddr_leng;

    char username[USERNAME_LENG];
    UT_hash_handle hh;
} Client;


#endif