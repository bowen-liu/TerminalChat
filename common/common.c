#include "common.h"
#include <string.h>


int hostname_to_ip(const char* hostname, const char* port, char* ip_return)
{
    int sockfd;  
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