#ifndef _FILE_TRANSFER_SERVER_H_
#define _FILE_TRANSFER_SERVER_H_

#include "server_common.h"
#include "../common/longsendrecv.h"
#include "group.h"

#define SERVER_TEMP_STORAGE "file_xfer_tmp"


typedef struct filexferargs_server {

    User *target;
    enum sendrecv_op operation;

    char filename[MAX_FILENAME];
    size_t filesize;
    size_t transferred;

} FileXferArgs_Server;


int client_transfer();
int new_client_transfer();
int accepted_file_transfer();


#endif