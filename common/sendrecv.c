#include "sendrecv.h"


/****************************/
/*          COMMON         */
/****************************/

int transfer_next_common(int socket, Pending_Msg *p)
{
    int bytes;
    
    if(p->pending_op == SENDING_OP)
    {
        bytes = send(socket, &p->pending_buffer[p->pending_transferred], p->pending_size, 0);
        if(bytes < 0)
        {
            perror("Failed to sent message to the socket...");
            return -1;
        }
    }
    else if(p->pending_op == RECVING_OP)
    {
        bytes = recv(socket, &p->pending_buffer[p->pending_transferred], p->pending_size, 0);
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
        p->pending_op = NO_XFER_OP;
    
    return bytes;
}


/****************************/
/*          SENDING         */
/****************************/

int send_msg_common(int socket, char* buffer, size_t size, Pending_Msg *p)
{
    int bytes;
    char headered_buf[BUFSIZE];
    size_t total_size;

    if(size == 0)
        return 0;

    //Only send a new message if there's no pending operation? We can also consider some kind of queueing...
    if(p->pending_op != NO_XFER_OP)
        return 0;

    //Limit this message if too long
    size = (size > MAX_MSG_LENG)? MAX_MSG_LENG : size;

    //Write the size of the message at the start of the buffer
    memcpy(&headered_buf[0], &(uint16_t){htons(size)}, sizeof(uint16_t));

    //Copy rest of the buffer behind the header.
    memcpy(&headered_buf[sizeof(uint16_t)], buffer, size);
    total_size = sizeof(uint16_t) + size;

    //Messages transmitted must be null terminated
    if(headered_buf[total_size-1] != '\0')
        headered_buf[total_size-1] = '\0';

    //Now try and send all of the headered buffer to the server
    //bytes = send(socket, headered_buf, total_size, 0);
    bytes = send(socket, headered_buf, (total_size>LONG_RECV_PAGE_SIZE)? LONG_RECV_PAGE_SIZE:total_size, 0);
    if(bytes < 0)
    {
        perror("Failed to sent message to the socket...");
        return -1;
    }

    printf("Expecting to send %zu bytes. Sent %d bytes.\n", size, bytes);

    //Incomplete send, try again later 
    if(bytes < size)
    {
        printf("Queued pending send! Expected %zu Received %u\n", size, bytes);

        p->pending_op = SENDING_OP;
        p->pending_size = size;
        p->pending_transferred = bytes;
        p->pending_buffer = buffer;
    }

    return bytes;
}



/****************************/
/*         RECEIVING        */
/****************************/

int recv_msg_common(int socket, char* buffer, size_t size, Pending_Msg *p)
{
    int bytes;
    uint16_t expected_length;

    //Only send a new message if there's no pending operation? We can also consider some kind of queueing...
    if(p->pending_op != NO_XFER_OP)
        return 0;
    
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

    printf("Expecting to receive %u bytes. Received %d bytes.\n", expected_length, bytes);

    //Incomplete receive
    if(bytes < expected_length)
    {
        printf("Queued pending recv! Expected %u Received %u\n", expected_length, bytes);
        
        p->pending_op = RECVING_OP;
        p->pending_size = expected_length;
        p->pending_transferred = bytes;
        p->pending_buffer = buffer;
    }

    return bytes;
}