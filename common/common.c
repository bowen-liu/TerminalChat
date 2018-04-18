#include "common.h"


void remove_newline(char *str)
{
    int last = strlen(str)-1;

    if(str[last] == '\n') 
        str[last] = '\0';
}

int register_fd_with_epoll(int epoll_fd, int socketfd)
{
    struct epoll_event new_event;
    const int event_flags = EPOLLIN;
    
    new_event.events = event_flags;
    new_event.data.fd = socketfd;

    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socketfd, &new_event) < 0) 
    {
        perror("Failed to register the new client socket with epoll!\n");
        close(socketfd);
        return 0;
    }

    return 1;
}  