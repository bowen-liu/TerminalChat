#include "file_transfer_client.h"
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>


/******************************/
/*           Helpers          */
/******************************/ 
void cleanup_transfer_args(FileXferArgs *args)
{
    munmap((void*)args->file_buffer, args->filesize);
    fclose(args->file_fp);
    memset(args, 0, sizeof(FileXferArgs));
}

void cancel_transfer(FileXferArgs *args)
{
    
}


/******************************/
/*          File Send         */
/******************************/ 

//Used by the sending client locally to parse its command into a FileXferArgs struct
void parse_send_cmd_sender(char *buffer, FileXferArgs *args)
{
    //You must fill args->socketfd before or after you call this function

    char *filename_start;

    memset(args, 0 ,sizeof(FileXferArgs));
    sscanf(buffer, "!sendfile=%[^,],size=%zu,target=%s", args->target_file, &args->filesize, args->target_name);

    //Extract the filename from the target file path
    filename_start = strrchr(args->target_file, '/');
    if(filename_start)
        strcpy(args->filename, ++filename_start);
    else
        strcpy(args->filename, args->target_file);
}


int new_send_cmd(FileXferArgs *args)
{
    char *filename_start;

    //Attempt to open the file for reading (binary mode)
    args->file_fp = fopen(args->target_file, "rb");
    if(!args->file_fp)
    {
        perror("Requested file not found!");
        return 0;
    }

    //Find the file's size
    fseek(args->file_fp, 0L,SEEK_END);
    args->filesize = ftell(args->file_fp);
    rewind(args->file_fp);

    //Open the file with mmap for reading
    args->file_buffer = mmap(NULL, args->filesize, PROT_READ, MAP_SHARED, fileno(args->file_fp), 0);
    if(!args->file_buffer)
    {
        perror("Failed to map sending file to memory.");
        fclose(args->file_fp);
        return 0;
    }

    args->operation = SENDING_OP;
    
    return 1;
}


int file_send_next(FileXferArgs *args)
{
    int remaining_size = args->filesize - args->transferred;
    int bytes;

    //Start of a new long send. Register the client's FD to signal on EPOLLOUT
    /*if(args->transferred == 0)
        update_epoll_events(epoll_fd, args->socketfd, CLIENT_EPOLL_DEFAULT_EVENTS | EPOLLOUT);*/
     
    //Send the next chunk to the client
    bytes = send(args->socketfd, &args->file_buffer[args->transferred], (remaining_size >= LONG_RECV_PAGE_SIZE)? LONG_RECV_PAGE_SIZE:remaining_size, 0);
    printf("Sending chunk (%d): %.*s\n", bytes, bytes, &args->file_buffer[args->transferred]);

    //Check exit conditions
    if(bytes <= 0)
    {
        perror("Failed to send long message");
        printf("Sent %zu\\%zu bytes to target \"%s\" before failure.\n", args->transferred, args->filesize, args->target_name);
    }
    else
    {
        args->transferred += bytes;
        printf("Sent %zu\\%zu bytes to client \"%s\"\n", args->transferred, args->filesize, args->target_name);

        if(args->transferred < args->filesize)
            return bytes;
    }

    //Cleanup resources when transfer completes or fails
    cleanup_transfer_args(args);

    //Remove the EPOLLOUT notification once long send has completed
    //update_epoll_events(epoll_fd, args->socketfd, CLIENT_EPOLL_DEFAULT_EVENTS);
    
    return 0;
}


/******************************/
/*          File Recv         */
/******************************/ 

//Used by the receiver and server to parse its command into a FileXferArgs struct
void parse_send_cmd_recver(char *buffer, FileXferArgs *args)
{
    memset(args, 0 ,sizeof(FileXferArgs));
    sscanf(buffer, "!sendfile=%[^,],size=%zu,target=%[^,],token=%s", args->filename, &args->filesize, args->target_name, args->token);

    //You must now fill args->socketfd yourself after calling this function, if you choose to accept the file afterwards
}


int new_recv_cmd(FileXferArgs *args)
{
    char recvpath[FILENAME_MAX+1];
    char filename[FILENAME_MAX+1];
    char *file_extension;

    int duplicate_files = 0;
    int retval;
    

    //Make a receiving folder from the target user, if it does not exist
    retval = mkdir(CLIENT_RECV_FOLDER, 0777);
    if(retval < 0 && errno != EEXIST)
    {
        perror("Failed to create directory for receiving.");
        return 0;
    }
 
    sprintf(recvpath, "%s/%s", CLIENT_RECV_FOLDER, args->target_name);
    retval = mkdir(recvpath, 0777);
    if(retval < 0 && errno != EEXIST)
    {
        perror("Failed to create directory for receiving.");
        return 0;
    }

    //Seperate the filename and extension
    strcpy(filename, args->filename);
    file_extension = strchr(filename, '.');
    if(file_extension)
    {
        *file_extension = '\0';
        ++file_extension;
    }

    //Check if there are any local files with the same filename already. If exists, append a number at the end.
    sprintf(args->target_file, "%s/%s", recvpath, args->filename);
    args->file_fp = fopen(args->target_file, "r");

    while(args->file_fp)
    {
        fclose(args->file_fp);

        sprintf(args->target_file, "%s/%s_%d", recvpath, filename, ++duplicate_files);
        if(file_extension)
        {
            strcat(args->target_file, ".");
            strcat(args->target_file, file_extension);
        }

        args->file_fp = fopen(args->target_file, "r");
    }
    printf("Created file \"%s\" for writing...\n", args->target_file);

    //Create a target file for appending (binary mode)
    args->file_fp = fopen(args->target_file, "ab");
    if(!args->file_fp)
    {
        perror("Cannot create file for writing.");
        return 0;
    }

    args->operation = RECVING_OP;

    return 1;
}


int file_recv_next(FileXferArgs *args)
{
    int bytes, i;
    char header[48];
    char *longcmd;

    bytes = recv(args->socketfd, &args->file_buffer[args->transferred], LONG_RECV_PAGE_SIZE, 0);

    //Check exit conditions
    if(bytes <= 0)
    {
        perror("Failed to receive long message from server.");
        printf("%zu\\%zu bytes received before failure.\n", args->transferred, args->filesize);
    }
    else
    {
        printf("Received chunk (%d): %.*s\n", bytes, (int)bytes, &args->file_buffer[args->transferred-1]);

        args->transferred += bytes;
        printf("Current Buffer: %.*s\n", (int)args->transferred, args->file_buffer);
        printf("%zu\\%zu bytes received\n", args->transferred, args->filesize);

        //Has the entire message been received?
        if(args->transferred < args->filesize)
            return bytes;

        printf("Completed file transfer!\n");
    }

    //Free resources used for the long recv
    cleanup_transfer_args(args);

    return 0;
}
