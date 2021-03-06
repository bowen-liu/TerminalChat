#ifndef _SERVER_H_
#define _SERVER_H_

#include "server_common.h"
#include "group.h"
#include "file_transfer_server.h"


#define UNREGISTERED_CONNECTION_TIMEOUT     30
#define CLIENT_EPOLL_DEFAULT_EVENTS         (EPOLLRDHUP | EPOLLIN)

/*Shared Server Variables*/

extern int server_socketfd;
extern struct sockaddr_in server_addr;
extern int connections_epollfd;

extern char *buffer;
extern char *msg_target;
extern char *msg_body;

extern Client *active_connections;                  //Hashtable of all active client sockets (key = socketfd)
extern User *active_users;                          //Hashtable of all active users (key = username), mapped to their client descriptors
extern Group* groups;                               //Hashtable of all user created private chatrooms (key = groupname)
extern IP_List *banned_ips;                         //Hashtable of all banned IPs
extern unsigned int total_users;    

extern Client *current_client;                      //Descriptor for the client being serviced right now

extern TimerEvent *timers;
extern int timers_epollfd;  


/*Send/recv*/
unsigned int send_msg(Client *c, char* buffer, size_t size);
unsigned int send_long_msg(Client *c, char* buffer, size_t size);
unsigned int send_bcast(char* buffer, size_t size);
unsigned int recv_msg(Client *c, char* buffer, size_t size);


#endif