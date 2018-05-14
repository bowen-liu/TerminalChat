#ifndef _FILE_TRANSFER_CLIENT_H_
#define _FILE_TRANSFER_CLIENT_H_

#include "../common/common.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>


#define CLIENT_RECV_FOLDER "files_received"

enum xfer_handshake_state {NEW_XFER = 0, REGISTRATION_SENT, TRANSFERING};

typedef struct {

    enum sendrecv_op operation;
    char target_name[USERNAME_LENG+1];
    char filename[MAX_FILENAME+1];

    int socketfd;
    enum xfer_handshake_state connection_state;
    char token[TRANSFER_TOKEN_SIZE+1];


    char target_file[MAX_FILE_PATH+1];
    FILE *file_fp;
    char *file_buffer;
    size_t filesize;
    size_t transferred;

} FileXferArgs;     //Also see FileXferArgs_Server



void cleanup_transfer_args(FileXferArgs *args);

/*Sending*/
void parse_send_cmd_sender(char *buffer, FileXferArgs *args);
int new_send_cmd(FileXferArgs *args);
int recver_accepted_file(char* buffer);

/*Receiving*/
void parse_send_cmd_recver(char *buffer, FileXferArgs *args);
void parse_accept_cmd(char *buffer, FileXferArgs *args);
int new_recv_cmd(FileXferArgs *args);

#endif