#include "common.h"
#include <sys/stat.h>


int hostname_to_ip(const char* hostname, const char* port, char* ip_return)
{ 
    struct addrinfo hints, *results;
    
    struct addrinfo *cur;
    struct sockaddr_in *cur_info;
 
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;                      //We only cares about ipv4 for now
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
 
    if(getaddrinfo(hostname, port, &hints, &results) != 0)
    {
        perror("Failed to resolve hostname");
        return 0;
    }

    //Walk through the list of results and pick ONE IP address (there usually is only one in the results list)
    for(cur = results; cur != NULL; cur = cur->ai_next) 
    {
        cur_info = (struct sockaddr_in *) cur->ai_addr;
        strcpy(ip_return , inet_ntoa(cur_info->sin_addr) );
    }
     
    freeaddrinfo(results);
    return 1;
}


void remove_newline(char *str)
{
    int last = strlen(str)-1;

    if(str[last] == '\n') 
        str[last] = '\0';
}

int register_fd_with_epoll(int epoll_fd, int socketfd, int event_flags)
{
    struct epoll_event new_event;
    
    new_event.events = event_flags;
    new_event.data.fd = socketfd;

    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socketfd, &new_event) < 0) 
    {
        perror("Failed to register the new socket with epoll!");
        close(socketfd);
        return 0;
    }

    return 1;
}  


int update_epoll_events(int epoll_fd, int socketfd, int event_flags)
{
    struct epoll_event new_event;
    
    new_event.events = event_flags;
    new_event.data.fd = socketfd;

    if(epoll_ctl(epoll_fd, EPOLL_CTL_MOD, socketfd, &new_event) < 0) 
    {
        perror("Failed to update trigger events with epoll!");
        return 0;
    }

    return 1;
}  


int name_is_valid(char* username)
{
    int i;
    
    if(strlen(username) > USERNAME_LENG)
    {
        printf("Username is too long.\n");
        return 0;
    }

    //Scan for invalid characters in the username. Alternatively, consider strpbrk()
    for(i=0; i<strlen(username); i++)
    {
        if(username[i] >= '0' && username[i] <= '9')
            continue;
        else if(username[i] >= 'A' && username[i] <= 'Z')
            continue;
        else if(username[i] >= 'a' && username[i] <= 'z')
            continue;
        else if(username[i] == '.' || username[i] == '_' || username[i] == '-')
            continue;
        else
        {
            printf("Found invalid character \'%c\' in the name. Names may only contain 0-9, A-Z, a-z, and \'.\', \'_\', \'-\'.\n", username[i]);
            return 0;
        }
    }

    return 1;
}


Namelist* find_from_namelist(Namelist* list, char *name)
{
    Namelist *curr, *tmp;

    LL_FOREACH_SAFE(list, curr, tmp)
    {
        if(strcmp(curr->name, name) == 0)
            break;
        else
            curr = NULL;
    }

    return curr;
}

int create_timerfd(int period_sec, int is_periodic, int epoll_fd)
{
    int timerfd;
    struct itimerspec timer_value;
    int epoll_events;
    
    //Create the timer
    timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if(timerfd < 0)
    {
        perror("Failed to create a timerfd.");
        return 0;
    }

    //Set the timer type (periodic or oneshot) and duration
    memset(&timer_value, 0, sizeof(struct itimerspec));
    timer_value.it_value.tv_sec = period_sec;

    if(is_periodic > 0)
    {
        epoll_events = EPOLLIN;
        timer_value.it_interval.tv_sec = period_sec;
    }
    else
        epoll_events = EPOLLIN | EPOLLONESHOT;
        

    //Register with epoll, if epoll_fd was specified
    if(epoll_fd && !register_fd_with_epoll(epoll_fd, timerfd, epoll_events))
    {
        close(timerfd);
        return 0;
    } 
    

    //Arm the timer
    if(timerfd_settime(timerfd, 0, &timer_value, NULL) < 0)
    {
        perror("Failed to arm event timer.");

        if(epoll_fd)
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, timerfd, NULL);
        close(timerfd);

        return 0;
    }

    return timerfd;
}

int path_is_file(const char *path)
{
    struct stat path_stat;
    stat(path, &path_stat);
    return S_ISREG(path_stat.st_mode);
}


int make_folder_and_file_for_writing(char* root_dir, char* target_name, char *filename, char* target_file_ret, FILE **file_fp_ret)
{
    char recvpath[FILENAME_MAX+1];
    char filename_only[FILENAME_MAX+1];
    char *file_extension;

    int duplicate_files = 0;
    int retval;
    
    //Make a receiving folder from the target user, if it does not exist
    retval = mkdir(root_dir, 0666);
    if(retval < 0 && errno != EEXIST)
    {
        perror("Failed to create directory for receiving.");
        return 0;
    }
 
    sprintf(recvpath, "%s/%s", root_dir, target_name);
    retval = mkdir(recvpath, 0666);
    if(retval < 0 && errno != EEXIST)
    {
        perror("Failed to create directory for receiving.");
        return 0;
    }

    //Seperate the filename and extension
    strcpy(filename_only, filename);
    file_extension = strchr(filename_only, '.');
    if(file_extension)
    {
        *file_extension = '\0';
        ++file_extension;
    }

    //Check if there are any local files with the same filename already. If exists, append a number at the end.
    sprintf(target_file_ret, "%s/%s", recvpath, filename);
    *file_fp_ret = fopen(target_file_ret, "r");

    while(*file_fp_ret)
    {
        fclose(*file_fp_ret);

        sprintf(target_file_ret, "%s/%s_%d", recvpath, filename_only, ++duplicate_files);
        if(file_extension)
        {
            strcat(target_file_ret, ".");
            strcat(target_file_ret, file_extension);
        }

        *file_fp_ret = fopen(target_file_ret, "r");
    }
    printf("Created file \"%s\" for writing...\n", target_file_ret);

    //mmap write is currently broken for WSL. We'll just append the received data for now. 
    
    //Create a target file for writing (binary mode)
    *file_fp_ret = fopen(target_file_ret, "ab");
    if(!*file_fp_ret)
    {
        perror("Cannot create file for writing.");
        return 0;
    }

    return 1;
}


int verify_received_file(size_t expected_size, unsigned int expected_crc, char* filepath)
{
    int filefd;
    struct stat fileinfo;
    char *filemap;
    unsigned int received_crc;

    //Open the received file for reading
    filefd = open(filepath, O_RDONLY);
    if(!filefd)
    {
        perror("Failed to open received file for verification.");
        return 0;
    }

    //Verify the received file's size
    fstat(filefd, &fileinfo);
    if(fileinfo.st_size != expected_size)
    {
        printf("Mismatched file size. Expected: %zu, Received: %zu\n", expected_size, fileinfo.st_size);
        close(filefd);
        return 0;
    }

    //Map the received file into memory and verify its checksum
    filemap = mmap(NULL, fileinfo.st_size, PROT_READ, MAP_SHARED, filefd, 0);
    if(!filemap)
    {
        perror("Failed to map received file in memory for verification.");
        close(filefd);
        return 0;
    }

    received_crc = xcrc32(filemap, fileinfo.st_size, CRC_INIT);
    close(filefd);
    munmap((void*)filemap, fileinfo.st_size);

    if(received_crc != expected_crc)
    {
        printf("Mismatched checksum. Expected: %x, Received: %x\n", expected_crc, received_crc);
        return 0;
    }

    printf("Received file \"%s\" is intact. Size: %zu, Checksum: %x\n", filepath, fileinfo.st_size, received_crc);
    return 1;
}