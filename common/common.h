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
#include "../library/uthash/utlist.h"                //http://troydhanson.github.io/uthash/utlist.html


#define USERNAME_LENG 32                             //Maximum length of usernames and group names
#define BUFSIZE 640                                  //Maxium size of single message, with headers included
#define EXTRA_CHARS 128                              //Maximum amount of extra characters used by headers
#define MAX_MSG_LENG (BUFSIZE - EXTRA_CHARS)         //Maximum size of messages can be entered by the user

#define LONG_RECV_PAGE_SIZE 32      //For testing long send/recv

void remove_newline(char *str);
int register_fd_with_epoll(int epoll_fd, int socketfd, int event_flags);
int update_epoll_events(int epoll_fd, int socketfd, int event_flags);
int name_is_valid(char* username);


#endif