#include "longsendrecv.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

/******************************/
/*          File Send         */
/******************************/ 

//Used by the sending client locally to parse its command into a FileXferArgs struct
FileXferArgs parse_send_cmd_sender(char *buffer)
{
    //You must fill args->socketfd before you call this function
    
    FileXferArgs args;
    char *filename_start;

    memset(&args, 0 ,sizeof(FileXferArgs));
    sscanf(buffer, "!sendfile=%[^,],size=%zu,target=%s", args.target_file, &args.filesize, args.target_name);

    //Extract the filename from the target file path
    filename_start = strrchr(args.target_file, '/');
    if(filename_start)
        strcpy(args.filename, ++filename_start);
    else
        strcpy(args.filename, args.target_file);

   return args;
}


int new_send_cmd(FileXferArgs *args)
{
    FILE *recving_file;
    char* filename_start;

    //Attempt to open the file and find its file size
    recving_file = fopen(args->target_file, "rb");
    if(!recving_file)
    {
        perror("Requested file not found!");
        return 0;
    }

    fseek(recving_file, 0L,SEEK_END);
    args->filesize = ftell(recving_file);
    
    return 1;
}



/******************************/
/*          File Recv         */
/******************************/ 

//Used by the receiver and server to parse its command into a FileXferArgs struct
FileXferArgs parse_send_cmd_recver(char *buffer)
{
    FileXferArgs args;

    memset(&args, 0 ,sizeof(FileXferArgs));
    sscanf(buffer, "!sendfile=%[^,],size=%zu,target=%s", args.filename, &args.filesize, args.target_name);
    sprintf(args.target_file, "%s/%s", args.target_name, args.filename);

    //You must now fill args->socketfd yourself after calling this function, if you choose to accept the file afterwards

    return args;
}


int new_recv_cmd(FileXferArgs *args)
{
    FILE *recving_file;
    char filename[FILENAME_MAX+1];
    char *file_extension;

    int duplicate_files = 0;
    int retval;
    
    //Make a receiving folder from the target user
    retval = mkdir(args->target_name, 0777);
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
    recving_file = fopen(args->target_file, "r");
    while(recving_file)
    {
        fclose(recving_file);

        sprintf(args->target_file, "%s/%s_%d", args->target_name, filename, ++duplicate_files);
        if(file_extension)
        {
            strcat(args->target_file, ".");
            strcat(args->target_file, file_extension);
        }

        recving_file = fopen(args->target_file, "r");
    }
    printf("Created file \"%s\" for writing...\n", args->target_file);

    //Create a target file for writing
    recving_file = fopen(args->target_file, "wb");
    if(!recving_file)
    {
        perror("Cannot create file for writing.");
        return 0;
    }

    return 1;
}
