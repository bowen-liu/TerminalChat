#include "common.h"
#include <string.h>


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


int username_is_valid(char* username)
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
            printf("Found invalid character \'%c\' in the username. Username may only contain 0-9, A-Z, a-z, and \'.\', \'_\', \'-\'.\n", username[i]);
            return 0;
        }
    }

    return 1;
}