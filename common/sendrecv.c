#include "sendrecv.h"


/****************************/
/*          COMMON          */
/****************************/

void clean_pending_msg(Pending_Msg *p)
{
    if(!p)
        return;
    
    if(p->pending_buffer)
        free(p->pending_buffer);

    memset(p, 0, sizeof(Pending_Msg));
}

int transfer_next_common(int socket, Pending_Msg *p)
{
    int bytes;
    size_t remaining_size;

    if(!p || p->pending_op == NO_XFER_OP)
    {
        printf("No operations to continue...\n");
        return 0;
    }
    
    remaining_size = p->pending_size - p->pending_transferred;

    if(p->pending_op == SENDING_OP)
    {
        printf("Continuing send operation...\n");

        bytes = send(socket, &p->pending_buffer[p->pending_transferred], remaining_size, 0);
        //bytes = send(socket, &p->pending_buffer[p->pending_transferred], (remaining_size>LONG_RECV_PAGE_SIZE)? LONG_RECV_PAGE_SIZE:remaining_size, 0);
        if(bytes < 0)
        {
            perror("Failed to sent message to the socket...");
            return -1;
        }
    }
    else if(p->pending_op == RECVING_OP)
    {
        printf("Continuing recv operation...\n");
        
        bytes = recv(socket, &p->pending_buffer[p->pending_transferred], remaining_size, 0);
        //bytes = recv(socket, &p->pending_buffer[p->pending_transferred], (remaining_size>LONG_RECV_PAGE_SIZE)? LONG_RECV_PAGE_SIZE:remaining_size, 0);
        if(bytes < 0)
        {
            perror("Failed to receive message from socket...");
            return -1;
        }
    }
    else
        return 0;

    p->pending_transferred += bytes;

    if(p->pending_transferred >= p->pending_size)
    {
        printf("Completed long transfer. %zu/%zu bytes transferred.\n", p->pending_transferred, p->pending_size);
        p->pending_op = NO_XFER_OP;
    }
        
    return bytes;
}


/****************************/
/*          SENDING         */
/****************************/

int send_direct(int socketfd, char* buffer, size_t size)
{
    return send(socketfd, buffer, size, 0);
}

static int send_msg_common_internal(int socket, char* buffer, size_t size, Pending_Msg *p)
{
    int bytes;
    char *headered_buf;
    size_t total_size;

    //Only send a new message if there's no pending operation? We can also consider some kind of queueing...
    if(p->pending_op != NO_XFER_OP)
    {
        printf("Target has a pending %s operation. Skipping sending new message...\n", (p->pending_op == SENDING_OP)? "SEND":"RECV");
        return 0;
    } 

    total_size = sizeof(uint16_t) + size;

    //Create a new headered message for sending
    headered_buf = malloc(total_size);
    memcpy(&headered_buf[0], &(uint16_t){htons(size)}, sizeof(uint16_t));
    memcpy(&headered_buf[sizeof(uint16_t)], buffer, size);
    
    if(headered_buf[total_size-1] != '\0')
        headered_buf[total_size-1] = '\0';

    //Now try and send all of the headered buffer to the server
    bytes = send(socket, headered_buf, total_size, 0);
    //bytes = send(socket, headered_buf, (total_size>LONG_RECV_PAGE_SIZE)? LONG_RECV_PAGE_SIZE:total_size, 0);
    if(bytes < 0)
    {
        perror("Failed to sent message to the socket...");
        return -1;
    }

    //printf("Expecting to send %zu bytes. Sent %d bytes.\n", total_size, bytes);

    //Incomplete send, try again later 
    if(bytes < total_size)
    {
        printf("Queued pending send! Expected %zu Received %u\n", total_size, bytes);

        p->pending_op = SENDING_OP;
        p->pending_size = total_size;
        p->pending_transferred = bytes;
        p->pending_buffer = headered_buf;

        printf("Sent Piece: \"%.*s\"\n", (int)(bytes - sizeof(uint16_t)), &p->pending_buffer[sizeof(uint16_t)]);
        printf("Send buffer: \"%s\"\n", &p->pending_buffer[sizeof(uint16_t)]);
    }
    else
        free(headered_buf);

    return bytes;
}

int send_msg_common(int socket, char* buffer, size_t size, Pending_Msg *p)
{
    if(size == 0)
        return 0;

    //Truncate the message if too long
    size = (size > MAX_MSG_LENG)? MAX_MSG_LENG : size;
    return send_msg_common_internal(socket, buffer, size, p);
}

//Will not truncate long messages. Intended for server to forward commands only
int send_msg_notruncate(int socket, char* buffer, size_t size, Pending_Msg *p)
{
    if(size == 0)
        return 0;

    return send_msg_common_internal(socket, buffer, size, p);
}


/****************************/
/*         RECEIVING        */
/****************************/

int recv_direct(int socketfd, char* buffer, size_t size)
{
    return recv(socketfd, buffer, size, 0);
}

int recv_msg_common(int socket, char* buffer, size_t size, Pending_Msg *p)
{
    int bytes;
    uint16_t expected_length;

    //Only send a new message if there's no pending operation? We can also consider some kind of queueing...
    if(p->pending_op != NO_XFER_OP)
    {
        printf("Target has a pending %s operation. Skipping receiving new message...\n", (p->pending_op == SENDING_OP)? "SEND":"RECV");
        return 0;
    }
    
    //First read the length of the expected message
    bytes = recv(socket, &expected_length, sizeof(uint16_t), 0);
    if(bytes <= 0)
    {
        perror("Failed to read message size from socket!");
        return 0;
    }
    expected_length = ntohs(expected_length);

    //Receive the remainder of the messages    
    bytes = recv(socket, buffer, expected_length, 0);
    //bytes = recv(socket, buffer, (expected_length>LONG_RECV_PAGE_SIZE)? LONG_RECV_PAGE_SIZE:expected_length, 0);
    if(bytes < 0)
    {
        perror("Failed to receive message from socket");
        return -1;
    }
    else if(bytes == 0)
    {
        printf("Socket has disconnected unexpectedly...\n");
        return -1;
    }

    //printf("Expecting to receive %u bytes. Received %d bytes.\n", expected_length, bytes);

    //Incomplete receive
    if(bytes < expected_length)
    {
        printf("Queued pending recv! Expected %u Received %u\n", expected_length, bytes);
        
        p->pending_op = RECVING_OP;
        p->pending_size = expected_length;
        p->pending_transferred = bytes;

        p->pending_buffer = malloc(expected_length);
        memcpy(p->pending_buffer, buffer, bytes);
        printf("Received Piece: %.*s\n", bytes, p->pending_buffer);
    }

    return bytes;
}