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


extern FileXferArgs *file_transfers;
extern FileInfo *incoming_transfers; 


/*Connection and helpers*/
void cancel_transfer(FileXferArgs *args);
void print_transfer_progress();


/*Ongoing sending and receiving*/
int file_send_next(FileXferArgs *args);
int file_recv_next(FileXferArgs *args);


/*Handle Server control messages*/
int incoming_file();
int recver_accepted_file();
int rejected_file_sending();
void file_transfer_cancelled();
int incoming_group_file();


/*Handle Client-side Operations*/
int outgoing_file();
int outgoing_file_group();
int accept_incoming_file();
int reject_incoming_file();
int cancel_ongoing_file_transfer();


#endif