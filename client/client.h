#ifndef _CLIENT_H_
#define _CLIENT_H_

#include "../common/common.h"
#include "commands.h"
#include "file_transfer_client.h"
#include "group.h"


#define MAX_EPOLL_EVENTS    32 
#define CLIENT_EPOLL_FLAGS         (EPOLLIN)


extern int my_socketfd;
extern struct sockaddr_in server_addr;
extern int epoll_fd;
extern char* my_username;


extern char *buffer;
extern char *msg_target;
extern char *msg_body;


int send_msg_client(char* buffer, size_t size);
int recv_msg_client(char* buffer, size_t size);


#endif