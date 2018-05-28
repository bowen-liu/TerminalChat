#ifndef _FILE_TRANSFER_SERVER_H_
#define _FILE_TRANSFER_SERVER_H_

#include "server_common.h"
#include "group.h"

#define GROUP_XFER_ROOT     "GROUP_FILES"

enum sendrecv_target {NO_TARGET = 0, USER_TARGET, GROUP_TARGET};

typedef struct {
    enum sendrecv_target target_type;
    
    union{
        User *user;
        Group *group;
    };

} XferTarget;


typedef struct filexferargs_server {

    //Myself
    int xfer_socketfd;
    enum sendrecv_op operation;
    User *myself;

    //Target
    enum sendrecv_target target_type;
    union{
        User *target_user;
        Group *target_group;
    };

    //File info
    char filename[MAX_FILENAME];
    size_t filesize;
    size_t transferred;
    unsigned int checksum;
    char token[TRANSFER_TOKEN_SIZE+1];
    
    //Used by SENDERs only
    TimerEvent *timeout;
    unsigned char* piece_buffer;
    size_t piece_size;
    size_t piece_transferred;

    //Used when users are putting files in a group
    char target_file[MAX_FILE_PATH+1];
    FILE *file_fp;

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

int put_new_file_to_group();
int get_new_file_from_group();


#endif