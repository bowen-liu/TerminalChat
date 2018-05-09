#ifndef _FILE_TRANSFER_SERVER_H_
#define _FILE_TRANSFER_SERVER_H_

#include "server_common.h"
#include "../common/longsendrecv.h"
#include "group.h"

#define SERVER_TEMP_STORAGE "file_xfer_tmp"

int client_transfer();
int new_client_transfer();


#endif