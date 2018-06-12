#ifndef _SENDRECV_COMMON_H_
#define _SENDRECV_COMMON_H_

#include "common.h"

#define MAX_MSG_SIZE    UINT16_MAX          //Maximum message that can be accepted with a single send_msg_common operation

enum sendrecv_op {NO_XFER_OP = 0, SENDING_OP, RECVING_OP};

typedef struct {

    enum sendrecv_op pending_op;
    char* pending_buffer;
    size_t pending_size;
    size_t pending_transferred;
    int segmented_msg :1;

} Pending_Msg;


int send_msg_common(int socket, char* buffer, size_t size, Pending_Msg *p);
int recv_msg_common(int socket, char* buffer, size_t size, Pending_Msg *p);

int transfer_next_common(int socket, Pending_Msg *p);
void clean_pending_msg(Pending_Msg *p);

#endif