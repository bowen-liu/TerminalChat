#ifndef _COMMON_H_
#define _COMMON_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h> 

#include <errno.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/timerfd.h>
#include <sys/mman.h>

#include "../library/uthash/uthash.h"                //http://troydhanson.github.io/uthash/  
#include "../library/uthash/utlist.h"                //http://troydhanson.github.io/uthash/utlist.html


#define DEFAULT_SERVER_PORT     6996

#define USERNAME_LENG           32                                  //Maximum length of usernames and group names
#define BUFSIZE                 4096                                //Buffer size used to hold a single received message
#define XFER_BUFSIZE            1048576                             //Buffer size used to hold a piece of data during file transfer
#define MAX_MSG_LENG            512                                 //Maximum size of messages can be entered by the user

#define LONG_RECV_PAGE_SIZE     32                                  //For testing long send/recv


/*File Transfer*/
#define MAX_FILENAME            128
#define MAX_FILE_PATH           512 + MAX_FILENAME
#define TRANSFER_TOKEN_SIZE     16
#define CRC_INIT                0xffffffff
#define XFER_REQUEST_TIMEOUT    600                                  //seconds


enum sendrecv_op {NO_XFER_OP = 0, SENDING_OP, RECVING_OP};

typedef struct namelist {
    char name[USERNAME_LENG+1];
    struct namelist *next;
} Namelist;


extern unsigned int xcrc32 (const unsigned char *buf, int len, unsigned int init);          //Defined in library/crc32/crc32.c

int hostname_to_ip(const char* hostname, const char* port, char* ip_return);
void remove_newline(char *str);
int register_fd_with_epoll(int epoll_fd, int socketfd, int event_flags);
int update_epoll_events(int epoll_fd, int socketfd, int event_flags);
int name_is_valid(char* username);
Namelist* find_from_namelist(Namelist* list, char *name);
int create_timerfd(int period_sec, int is_periodic, int epoll_fd);
int make_folder_and_file_for_writing(char* root_dir, char* target_name, char *filename, char* target_file_ret, FILE **file_fp_ret);
int verify_received_file(size_t expected_size, unsigned int expected_crc, char* filepath);


#endif