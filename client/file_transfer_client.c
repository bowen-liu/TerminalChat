#include "file_transfer_client.h"
#include "client.h"

#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>



/******************************/
/*           Helpers          */
/******************************/ 

int path_is_file(const char *path)
{
    struct stat path_stat;
    stat(path, &path_stat);
    return S_ISREG(path_stat.st_mode);
}

void cancel_transfer(FileXferArgs *args)
{
    //Close network connections
    if(args->socketfd)
    {
        if(epoll_ctl(epoll_fd, EPOLL_CTL_DEL, args->socketfd, NULL) < 0) 
            perror("Failed to unregister the disconnected client from epoll!");
        close(args->socketfd);
        
        printf("Closing transfer connection (%s) with \"%s\" \n", 
                (args->operation == SENDING_OP)? "SEND":"RECV", args->target_name);
    }

    //Destroy the progress timer
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, args->timerfd, NULL);
    close(args->timerfd);
    
    //Free or unmap transfer buffers, and close files
    if(args->operation == SENDING_OP)
        munmap((void*)args->file_buffer, args->filesize);
    else
        free(args->file_buffer);
    fclose(args->file_fp);
    
    //Free the transfer args object
    free(args);
    file_transfers = NULL;
}


static int new_transfer_connection(FileXferArgs *args)
{
    args->socketfd = socket(AF_INET, SOCK_STREAM, 0);

    if(!args->socketfd)
    {
        perror("Failed to create socket for new transfer connection.");
        return 0;
    }

    if(connect(args->socketfd, (struct sockaddr*) &server_addr, sizeof(struct sockaddr_in)) < 0)
    {
        perror("Failed to connect for new transfer connection.");
        return 0;
    }

    //Wait for server greeting
    if(!recv_msg_client(args->socketfd, buffer, BUFSIZE))
    {
        cancel_transfer(args);
        return 0;
    }
    printf("%s\n", buffer);

    return args->socketfd;
}


FileInfo* find_pending_xfer(char *sender_name)
{
    FileInfo *curr, *temp; 

    //Find the file associated with the sender
    LL_FOREACH_SAFE(incoming_transfers, curr, temp)
    {
        if(strcmp(curr->target_name, sender_name) == 0)
            break;
        else
            curr = NULL;
    }
    
    return curr;
}


unsigned int delete_pending_xfer(char *sender_name)
{
    unsigned int count = 0;
    FileInfo *curr, *temp; 

    LL_FOREACH_SAFE(incoming_transfers, curr, temp)
    {
        if(strcmp(curr->target_name, sender_name) == 0)
        {
            LL_DELETE(incoming_transfers, curr);
            free(curr);
            ++count;
        }
    }
    
    return count;
}

void print_transfer_progress()
{
    uint64_t timer_retval;
    float percent_completed;

    size_t transferred_this_period;
    float speed;
    char *speed_unit;

    if(!read(file_transfers->timerfd, &timer_retval, sizeof(uint64_t)))
        perror("Failed to read timer event.");

    //Print amount received so far, and completion percentage
    percent_completed = ((float)file_transfers->transferred / file_transfers->filesize) * 100;
    printf("%s %zu/%zu bytes (%.2f %%). ", 
              (file_transfers->operation == SENDING_OP)? "Sent":"Received", file_transfers->transferred, file_transfers->filesize, percent_completed);

    //Estimate the current transfer speed
    transferred_this_period = file_transfers->transferred - file_transfers->last_transferred;
    file_transfers->last_transferred = file_transfers->transferred;

    if(transferred_this_period > 1000000)
    {
        speed = (float)transferred_this_period / 1000000;
        speed_unit = "MB/s";
    }
    else if(transferred_this_period > 1000)
    {
        speed = (float)transferred_this_period / 1000;
        speed_unit = "KB/s";
    }
    else
    {
        speed = (float)transferred_this_period;
        speed_unit = "B/s";
    }

    printf("%.2f %s\n", speed, speed_unit);
}


/******************************/
/*          File Send         */
/******************************/ 

//Used by the sending client locally to parse its command into a FileXferArgs struct
void parse_send_cmd_sender(char *buffer, FileXferArgs *args, int target_is_group)
{
    //You must fill args->socketfd before or after you call this function

    char *filename_start;

    memset(args, 0 ,sizeof(FileXferArgs));

    if(target_is_group)
        sscanf(buffer, "!putfile=%[^,],target=%s", args->target_file, args->target_name);
    else
        sscanf(buffer, "!sendfile=%[^,],target=%s", args->target_file, args->target_name);

    //Extract the filename from the target file path
    filename_start = strrchr(args->target_file, '/');
    if(filename_start)
        strcpy(args->filename, ++filename_start);
    else
        strcpy(args->filename, args->target_file);
}


//Used by the sending client locally to parse its command into a FileXferArgs struct
void parse_accept_cmd(char *buffer, FileXferArgs *args)
{
    memset(args, 0 ,sizeof(FileXferArgs));
    sscanf(buffer, "!acceptfile=%[^,],size=%zu,crc=%x,target=%[^,],token=%s", 
            args->filename, &args->filesize, &args->checksum, args->target_name, args->token);
}

static int load_sending_file(FileXferArgs *args)
{    
    //Attempt to open the file for reading (binary mode)
    args->file_fp = fopen(args->target_file, "rb");
    if(!args->file_fp)
    {
        perror("Requested file not found!");
        return 0;
    }

    //Make sure this is a regular file and not a directory
    if(!path_is_file(args->target_file))
    {
        printf("Cannot send \"%s\". This is not a regular file.\n", args->target_file);
        fclose(args->file_fp);
        return 0;
    }

    //Find the file's size
    fseek(args->file_fp, 0L,SEEK_END);
    args->filesize = ftell(args->file_fp);
    rewind(args->file_fp);

    //Open the file with mmap for reading
    args->file_buffer = mmap(NULL, args->filesize, PROT_READ, MAP_SHARED, fileno(args->file_fp), 0);
    if(args->file_buffer == MAP_FAILED)
    {
        perror("Failed to map sending file to memory.");
        fclose(args->file_fp);
        return 0;
    }

    //Calculate the file's checksum (crc32)
    args->checksum = xcrc32(args->file_buffer, args->filesize, CRC_INIT);
}


int new_send_cmd(FileXferArgs *args)
{
    if(!load_sending_file(args))
        return 0;

    args->operation = SENDING_OP;

    //Rewrite the existing message in buffer with the de-localized filename
    sprintf(buffer, "!sendfile=%s,size=%zu,crc=%x,target=%s", 
            file_transfers->filename, file_transfers->filesize, file_transfers->checksum, file_transfers->target_name);
    printf("Initiating file transfer with user \"%s\" for file \"%s\" (%zu bytes, checksum: %x)\n", 
            file_transfers->target_name, file_transfers->filename, file_transfers->filesize, file_transfers->checksum);
    
    //The new message in buffer will be sent automatically when this function returns back to client_main_loop()

    return 1;
}


int recver_accepted_file(char* buffer)
{
    char accepted_filename[FILENAME_MAX+1];
    size_t accepted_filesize;
    unsigned int accepted_checksum;
    char accepted_target_name[USERNAME_LENG+1];
    char accepted_token[TRANSFER_TOKEN_SIZE+1];

    int matches = 0;

    sscanf(buffer, "!acceptfile=%[^,],size=%zu,crc=%x,target=%[^,],token=%s", 
            accepted_filename, &accepted_filesize, &accepted_checksum, accepted_target_name, accepted_token);

    //Validate this transfer matches what we intended to send
    if(file_transfers)
        if(strcmp(file_transfers->target_name, accepted_target_name) == 0)
            if(strcmp(file_transfers->filename, accepted_filename) == 0)
                if(file_transfers->filesize == accepted_filesize)
                    if(file_transfers->checksum == accepted_checksum)
                        matches = 1;
    
    if(!matches)
    {
        printf("No pending file send for file \"%s\" (%zu bytes, checksum: %x) for user \"%s\".\n",
                accepted_filename, accepted_filesize, accepted_checksum, accepted_target_name);
        return 0;
    }

    printf("Receiver \"%s\" has accepted to receive the file \"%s\" (%zu bytes, token: %s, checksum: %x)!\n",
            accepted_filename, accepted_target_name, accepted_filesize, accepted_token, accepted_checksum);
    
    //Record the server assigned token
    strcpy(file_transfers->token, accepted_token);



    /*******************************************************/
    /* Open new connection to server for file transferring */
    /*******************************************************/

    if(!new_transfer_connection(file_transfers))
    {
        cancel_transfer(file_transfers);
        return 0;
    }

    //Tell server I'm using this connection to download a file
    sprintf(buffer, "!xfersend=%s,size=%zu,crc=%x,sender=%s,recver=%s,token=%s", 
            file_transfers->filename, file_transfers->filesize, file_transfers->checksum, my_username, file_transfers->target_name, file_transfers->token);
    
    if(!send_msg_client(file_transfers->socketfd, buffer, strlen(buffer)+1))
    {
        perror("Failed to send transfer registration.");
        cancel_transfer(file_transfers);
        return 0;
    }

    //Obtain a response from the server
    if(!recv_msg_client(file_transfers->socketfd, buffer, BUFSIZE))
    {
        cancel_transfer(file_transfers);
        return 0;
    }

    if(strcmp(buffer, "Accepted") != 0)
    {
        printf("Server did not accept transfer connection: \"%s\"\n", buffer);
        cancel_transfer(file_transfers);
        return 0;
    }
        
    printf("Sender has successfully established to the server!\n");

    //Register the new connection with epoll and set it as nonblocking
    fcntl(file_transfers->socketfd, F_SETFL, O_NONBLOCK);
    register_fd_with_epoll(epoll_fd, file_transfers->socketfd, EPOLLOUT | EPOLLRDHUP);

    //Create a timerfd to periodically print the transfer progress
    file_transfers->timerfd = create_timerfd(PRINT_XFER_PROGRESS_PERIOD, 1, epoll_fd);

    return file_transfers->socketfd;
}


int file_send_next(FileXferArgs *args)
{
    size_t remaining_size = args->filesize - args->transferred;
    int bytes;
     
    //Send the next chunk to the client
    //bytes = send_direct_client(args->socketfd, &args->file_buffer[args->transferred], remaining_size);
    bytes = send_direct_client(args->socketfd, &args->file_buffer[args->transferred], (remaining_size < RECV_CHUNK_SIZE)? remaining_size:RECV_CHUNK_SIZE);

    if(bytes <= 0)
    {
        perror("Failed to send long message");
        printf("Sent %zu\\%zu bytes to target \"%s\" before failure.\n", args->transferred, args->filesize, args->target_name);
        cancel_transfer(args);
        return 0;
    }

    args->transferred += bytes;
    //printf("Sent %zu\\%zu bytes to client \"%s\"\n", args->transferred, args->filesize, args->target_name);
    //sleep(1);

    if(args->transferred < args->filesize)
        return bytes;

    //Transfer has completed!
    print_transfer_progress();
    printf("Completed file transfer! Waiting for server to close the transfer connection...\n");

    //Leave the sending transfer connection idle, and wait for the server to close it (recver has received all pending byes)
    update_epoll_events(epoll_fd, args->socketfd, EPOLLRDHUP);

    return bytes;
}





/******************************/
/*          File Recv         */
/******************************/ 

//Used by the receiver and server to parse its command into a FileXferArgs struct
void parse_send_cmd_recver(char *buffer, FileInfo *fileinfo)
{
    memset(fileinfo, 0 ,sizeof(FileInfo));
    sscanf(buffer, "!sendfile=%[^,],size=%zu,crc=%x,target=%[^,],token=%s", 
            fileinfo->filename, &fileinfo->filesize, &fileinfo->checksum, fileinfo->target_name, fileinfo->token);

    //You must now fill args->socketfd yourself after calling this function, if you choose to accept the file afterwards
}


int new_recv_cmd(FileXferArgs *args)
{
    char recvpath[FILENAME_MAX+1];
    char filename[FILENAME_MAX+1];
    char *file_extension;

    int duplicate_files = 0;
    int retval;
    
    
    if(!make_folder_and_file_for_writing(CLIENT_RECV_FOLDER, args->target_name, args->filename, args->target_file, &args->file_fp))
        return 0;

    args->file_buffer = malloc(RECV_CHUNK_SIZE);
    args->operation = RECVING_OP;

    //Tell the server I am accepting this file
    sprintf(buffer, "!acceptfile=%s,size=%zu,crc=%x,target=%s,token=%s", 
            file_transfers->filename, file_transfers->filesize, file_transfers->checksum, file_transfers->target_name, file_transfers->token);
    send_msg_client(my_socketfd, buffer, strlen(buffer)+1);



    /*******************************************************/
    /* Open new connection to server for file transferring */
    /*******************************************************/

    if(!new_transfer_connection(args))
    {
        cancel_transfer(args);
        return 0;
    }

    //Tell server I'm using this connection to download a file
    sprintf(buffer, "!xferrecv=%s,size=%zu,crc=%x,sender=%s,recver=%s,token=%s", 
            file_transfers->filename, file_transfers->filesize, file_transfers->checksum, file_transfers->target_name, my_username, file_transfers->token);
    if(!send_msg_client(args->socketfd, buffer, strlen(buffer)+1))
    {
        perror("Failed to send transfer registration.");
        cancel_transfer(args);
        return 0;
    }

    //Obtain a response from the server
    if(!recv_msg_client(args->socketfd, buffer, BUFSIZE))
    {
        cancel_transfer(args);
        return 0;
    }

    if(strcmp(buffer, "Accepted") != 0)
    {
        printf("Server did not accept transfer connection: \"%s\"\n", buffer);
        cancel_transfer(args);
        return 0;
    }
        
    printf("Receiver has successfully established to the server!\n");

    //Register the new connection with epoll and set it as nonblocking
    fcntl(args->socketfd, F_SETFL, O_NONBLOCK);
    register_fd_with_epoll(epoll_fd, args->socketfd, EPOLLIN | EPOLLRDHUP);

    //Create a timerfd to periodically print the transfer progress
    file_transfers->timerfd = create_timerfd(PRINT_XFER_PROGRESS_PERIOD, 1, epoll_fd);

    return args->socketfd;
}


int file_recv_next(FileXferArgs *args)
{
    size_t remaining_size = args->filesize - args->transferred;
    int bytes;

    bytes = recv(args->socketfd, args->file_buffer, (remaining_size < RECV_CHUNK_SIZE)? remaining_size:RECV_CHUNK_SIZE, 0);

    if(bytes <= 0)
    {
        perror("Failed to receive long message from server.");
        printf("%zu\\%zu bytes received before failure.\n", args->transferred, args->filesize);
        cancel_transfer(args);
        return 0;
    }
    
    //Append the received chunk to local file
    if(write(fileno(args->file_fp), args->file_buffer, bytes) != bytes)
    {
        perror("Failed to write correct number of bytes to receiving file.");
    }

    args->transferred += bytes;
   // printf("Received %zu\\%zu bytes from \"%s\"\n", args->transferred, args->filesize, args->target_name);

    //Has the entire message been received?
    if(args->transferred < args->filesize)
        return bytes;
    
    //Transfer has completed!
    print_transfer_progress();
    printf("Completed file transfer!\n");
    
    //Verify file integrity, and then cleanup and close the transfer connection
    verify_received_file(args->filesize, args->checksum, args->target_file);
    cancel_transfer(args);
    return bytes;
}





/******************************/
/* Client-Group File Sharing */
/******************************/ 

int put_file_to_group(FileXferArgs *args)
{
    if(!load_sending_file(args))
        return 0;

    args->operation = SENDING_OP;

    //Rewrite the existing message in buffer with the de-localized filename
    sprintf(buffer, "!putfile=%s,size=%zu,crc=%x,target=%s", 
            file_transfers->filename, file_transfers->filesize, file_transfers->checksum, file_transfers->target_name);
    printf("Initiating file put with group \"%s\" for file \"%s\" (%zu bytes, checksum: %x)\n", 
            file_transfers->target_name, file_transfers->filename, file_transfers->filesize, file_transfers->checksum);
    
    //The new message in buffer will be sent automatically when this function returns back to client_main_loop()

    return 1;
}

int get_file_from_group()
{

}