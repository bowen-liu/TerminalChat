#ifndef _SERVER_COMMON_H_
#define _SERVER_COMMON_H_

#include "../common/common.h"  

struct user;
struct group_list;
struct filexferargs_server;
struct timerevent;

enum connection_type {UNREGISTERED_CONNECTION = 0, USER_CONNECTION, TRANSFER_CONNECTION};

//Abstracts each active client participating in the server
typedef struct {
    
    /*Connection info*/
    int socketfd;                        //Key used for the main active client hash table
    struct sockaddr_in sockaddr;
    int sockaddr_leng;
    enum connection_type connection_type;

    /*Descriptors for other server components*/
    struct user *user;
    struct filexferargs_server *file_transfers;
    struct timerevent *idle_timer;

    UT_hash_handle hh;
} Client;


//Maps a username to a client object
typedef struct user {

    /*Main user info*/
    char username[USERNAME_LENG+1];
    unsigned int is_admin :1;
    Client *c;

    /*Pending Long Message (if any)*/
    Pending_Msg pending_msg;

    /*Descriptors for other server components*/
    struct grouplist *groups_joined;
    
    UT_hash_handle hh;
} User;


enum timer_event_type {NO_EVENT = 0, EXPIRING_UNREGISTERED_CONNECTION, EXPIRING_TRANSFER_REQ};

typedef struct timerevent{
    int timerfd;
    enum timer_event_type event_type;
    Client *c;

    UT_hash_handle hh;
} TimerEvent;



User* get_current_client_user();
void send_error_code(Client *c, enum error_codes err, char *additional_info);
void kill_connection(int *socketfd);
void disconnect_client(Client *c, char *reason);
void cleanup_timer_event(TimerEvent *timer);
unsigned int handle_new_username(char *requested_name, char *new_username_ret);


#endif