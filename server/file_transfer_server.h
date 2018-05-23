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
    
    TimerEvent *timeout;

    //Used by SENDERs only to deal with partial sends
    unsigned char* piece_buffer;
    size_t piece_size;
    size_t piece_transferred;
    unsigned int next_piece_ready :1;

} FileXferArgs_Server;


void cleanup_transfer_connection(Client *c);
int close_associated_xfer_connection(Client *c);
void cancel_user_transfer(Client *c);
int transfer_invite_expired(Client *c);

int register_recv_transfer_connection();
int register_send_transfer_connection();


int new_client_transfer();
int accepted_file_transfer();
int rejected_file_transfer();
int user_cancelled_transfer();

int client_data_forward_recver_ready();
int client_data_forward_sender_ready();


#endif