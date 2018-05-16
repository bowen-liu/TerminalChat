#ifndef _FILE_TRANSFER_SERVER_H_
#define _FILE_TRANSFER_SERVER_H_

#include "server_common.h"
#include "group.h"

#define SERVER_TEMP_STORAGE "file_xfer_tmp"


typedef struct filexferargs_server {

    int xfer_socketfd;
    enum sendrecv_op operation;
    User *myself;
    User *target;

    char filename[MAX_FILENAME];
    size_t filesize;
    size_t transferred;
    unsigned int checksum;
    char token[TRANSFER_TOKEN_SIZE+1];

} FileXferArgs_Server;


int register_recv_transfer_connection();
int register_send_transfer_connection();

int client_data_forward(char *buffer, size_t bytes);
int new_client_transfer();
int accepted_file_transfer();


#endif