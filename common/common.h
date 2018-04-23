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


#define BUFSIZE 1024
#define USERNAME_LENG 32
#define EXTRA_CHARS 48
#define MAX_MSG_LENG (BUFSIZE - USERNAME_LENG - EXTRA_CHARS)         //Reserve some extra bytes for other extra chars appended by the server   

void remove_newline(char *str);
int register_fd_with_epoll(int epoll_fd, int socketfd, int event_flags);
int update_epoll_events(int epoll_fd, int socketfd, int event_flags);
int username_is_valid(char* username);


#endif