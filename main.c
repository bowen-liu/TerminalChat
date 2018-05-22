#include "common/common.h"

extern void server(const char* hostname, const unsigned int port);
extern void client(const char* hostname, const unsigned int port, char *username);


int main(int argc, char *argv[])
{
    //TODO: use getopt
    char ipaddr[INET_ADDRSTRLEN];
    unsigned int port = 0;
    
    if(argc < 2)
    {
        printf("Not enough args...\n");
        return 0;
    }

    //Server
    if(strcmp(argv[1], "-s") == 0)
    {
        if(argc < 3)
            server(NULL, DEFAULT_SERVER_PORT);
       
        else
        {
            sscanf(argv[2], "%[^:]:%u", ipaddr, &port);
            if(port == 0)
                port = DEFAULT_SERVER_PORT;
            
            server(ipaddr, port);
        }
    }  

    //Client
    else
    {
        if(argc < 4)
        {
            printf("Not enough args for client...\n");
            return 0;
        }
        
        sscanf(argv[3], "%[^:]:%u", ipaddr, &port);
        if(port == 0)
                port = DEFAULT_SERVER_PORT;

        client(ipaddr, port, argv[2]);
    }
        
}