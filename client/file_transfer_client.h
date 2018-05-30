#ifndef _FILE_TRANSFER_CLIENT_H_
#define _FILE_TRANSFER_CLIENT_H_

#include "../common/common.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/timerfd.h>


#define RECV_CHUNK_SIZE             BUFSIZE
//#define RECV_CHUNK_SIZE           64
#define CLIENT_RECV_FOLDER          "files_received"
#define PRINT_XFER_PROGRESS_PERIOD  1


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

    //Timer that periodically notifies the current transfer's progress
    int timerfd;
    size_t last_transferred;        

} FileXferArgs;         //Also see FileXferArgs_Server


typedef struct fileinfo{

    char target_name[USERNAME_LENG+1];
    enum sendrecv_target target_type;

    char filename[MAX_FILENAME+1];
    size_t filesize;
    unsigned int checksum;
    char token[TRANSFER_TOKEN_SIZE+1];

    struct fileinfo *next;

} FileInfo;


/*Connection and helpers*/
void cancel_transfer(FileXferArgs *args);
FileInfo* find_pending_xfer(char *sender_name);
unsigned int delete_pending_xfer(char *sender_name);
void print_transfer_progress();


/*Sending*/
void parse_send_cmd_sender(char *buffer, FileXferArgs *args, int target_is_group);
int new_send_cmd(FileXferArgs *args);
int recver_accepted_file(char* buffer);
int file_send_next(FileXferArgs *args);


/*Receiving*/
void parse_send_cmd_recver(char *buffer, FileInfo *fileinfo);
void parse_accept_cmd(char *buffer, FileXferArgs *args);
int new_recv_connection(FileXferArgs *args);
int file_recv_next(FileXferArgs *args);

/*Group Send/Recv*/
int put_file_to_group(FileXferArgs *args);
int get_file_from_group();

#endif