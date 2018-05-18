#ifndef _FILE_TRANSFER_CLIENT_H_
#define _FILE_TRANSFER_CLIENT_H_

#include "../common/common.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>


#define RECV_CHUNK_SIZE     BUFSIZE
//#define RECV_CHUNK_SIZE     64
#define CLIENT_RECV_FOLDER "files_received"


typedef struct {

    enum sendrecv_op operation;
    char target_name[USERNAME_LENG+1];
    char filename[MAX_FILENAME+1];

    int socketfd;
    char token[TRANSFER_TOKEN_SIZE+1];

    char target_file[MAX_FILE_PATH+1];
    FILE *file_fp;
    char *file_buffer;
    size_t filesize;
    size_t transferred;
    unsigned int checksum;

} FileXferArgs;     //Also see FileXferArgs_Server


void cancel_transfer(FileXferArgs *args);


/*Sending*/
void parse_send_cmd_sender(char *buffer, FileXferArgs *args);
int new_send_cmd(FileXferArgs *args);
int recver_accepted_file(char* buffer);
int file_send_next(FileXferArgs *args);


/*Receiving*/
void parse_send_cmd_recver(char *buffer, FileXferArgs *args);
void parse_accept_cmd(char *buffer, FileXferArgs *args);
int new_recv_cmd(FileXferArgs *args);
int file_recv_next(FileXferArgs *args);

#endif