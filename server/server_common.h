#ifndef _SERVER_COMMON_H_
#define _SERVER_COMMON_H_

#include "../common/common.h"  
   

struct namelist;
struct filexferargs_server;

enum connection_type {UNREGISTERED_CONNECTION = 0, USER_CONNECTION, TRANSFER_CONNECTION};

//Abstracts each active client participating in the server
typedef struct {
    
    /*Connection info*/
    int socketfd;                        //Key used for the main active client hash table
    struct sockaddr_in sockaddr;
    int sockaddr_leng;
    enum connection_type connection_type;

    /*Userinfo*/
    char username[USERNAME_LENG+1];
    int is_admin :1;

    /*Pending Long Message (if any)*/
    char* pending_buffer;
    size_t pending_size;
    size_t pending_processed;

    /*Descriptors for other server components*/
    struct namelist *groups_joined;
    struct filexferargs_server *file_transfers;

    UT_hash_handle hh;
} Client;


//Maps a username to a client object
typedef struct {
    char username[USERNAME_LENG+1];
    Client *c;
    UT_hash_handle hh;
} User;



User* get_current_client_user();
void kill_connection(int socketfd);
void disconnect_client(Client *c);

#endif