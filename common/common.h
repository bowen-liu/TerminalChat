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

#include "../library/uthash/uthash.h"                //http://troydhanson.github.io/uthash/  


#define BUFSIZE 512
#define USERNAME_LENG 32
#define EXTRA_CHARS 48
#define MAX_MSG_LENG (BUFSIZE - USERNAME_LENG - EXTRA_CHARS)         //Reserve some extra bytes for other extra chars appended by the server   

#define LONG_RECV_PAGE_SIZE 32      //For testing long send/recv

void remove_newline(char *str);
int register_fd_with_epoll(int epoll_fd, int socketfd, int event_flags);
int update_epoll_events(int epoll_fd, int socketfd, int event_flags);
int username_is_valid(char* username);


#endif