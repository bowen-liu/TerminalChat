#ifndef _COMMON_H_
#define _COMMON_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h> 

#include <sys/epoll.h>
#include <fcntl.h>


#define BUFSIZE 512
#define USERNAME_LENG 32
#define MAX_MSG_LENG (BUFSIZE - USERNAME_LENG - 3)      

int register_fd_with_epoll(int epollfd, int socketfd);


#endif