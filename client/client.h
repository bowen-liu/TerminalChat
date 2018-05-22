#ifndef _CLIENT_H_
#define _CLIENT_H_

#include "../common/common.h"
#include "file_transfer_client.h"

#define MAX_EPOLL_EVENTS    32 
#define CLIENT_EPOLL_FLAGS         (EPOLLIN)


typedef struct {
    
    char username[USERNAME_LENG+1];
    UT_hash_handle hh;

} Member;


extern int my_socketfd;
extern struct sockaddr_in server_addr;
extern int epoll_fd;
extern char* my_username;
extern FileXferArgs *file_transfers;
extern FileInfo *incoming_transfers; 

extern char *buffer;

unsigned int send_direct_client(int socket, char* buffer, size_t size);
unsigned int send_msg_client(int socket, char* buffer, size_t size);
unsigned int recv_msg_client(int socket, char* buffer, size_t size);


#endif