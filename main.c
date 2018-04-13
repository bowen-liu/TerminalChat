#include "common/common.h"


#define SERVER_IP   "127.0.0.1"
#define SERVER_PORT 6996

extern void server(const char* ipaddr, const int port);
extern void client(const char* ipaddr, const int port, char *username);


int main(int argc, char *argv[])
{
    if(argc < 2)
    {
        printf("Not enough args...\n");
        return 0;
    }

    if(strcmp(argv[1], "-s") == 0)
    {
        printf("Running as server...\n");
        server(SERVER_IP, SERVER_PORT);
    }  
    else
    {
        if(argc < 3)
        {
            printf("Not enough args for client...\n");
            return 0;
        }
        
        printf("Running as Client...\n");
        client(SERVER_IP, SERVER_PORT, argv[2]);
    }
        
}