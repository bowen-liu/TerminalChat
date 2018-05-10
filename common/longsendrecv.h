#ifndef _LONGSENDRECV_H_
#define _LONGSENDRECV_H_

#include "../common/common.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>


#define MAX_FILENAME    128
#define MAX_FILE_PATH   512 + MAX_FILENAME

#define CLIENT_RECV_FOLDER "files_received"


typedef struct {

    int socketfd;
    enum sendrecv_op operation;
    char target_name[USERNAME_LENG+1];
    char filename[MAX_FILENAME];

    char target_file[MAX_FILE_PATH];
    FILE *file_fp;
    char *file_buffer;
    size_t filesize;
    size_t transferred;

} FileXferArgs;     //Also see FileXferArgs_Server



void cleanup_transfer_args(FileXferArgs *args);

FileXferArgs parse_send_cmd_sender(char *buffer);
int new_send_cmd(FileXferArgs *args);

FileXferArgs parse_send_cmd_recver(char *buffer);
int new_recv_cmd(FileXferArgs *args);

#endif