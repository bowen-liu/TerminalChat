#include "common/common.h"


#if defined(SERVER_BUILD) && !defined(CLIENT_BUILD)

    extern void server(const char* hostname, const unsigned int port);

    int main(int argc, char *argv[])
    {
        //TODO: use getopt
        char ipaddr[INET_ADDRSTRLEN];
        unsigned int port = 0;
    
        if(argc < 2)
        {
            printf("Server usage: chatserver <ip>:<port>\n");
            printf("Binding to INADDR_ANY on default port...\n");
            server(NULL, DEFAULT_SERVER_PORT);
        }
        else
        {
            sscanf(argv[1], "%[^:]:%u", ipaddr, &port);
            if(port == 0)
                port = DEFAULT_SERVER_PORT;
            
            server(ipaddr, port);
        } 
    }

#elif defined(CLIENT_BUILD) && !defined(SERVER_BUILD)

    extern void client(const char* hostname, const unsigned int port, char *username);

    int main(int argc, char *argv[])
    {
        //TODO: use getopt
        char ipaddr[INET_ADDRSTRLEN];
        unsigned int port = 0;
        
        if(argc < 3)
        {
            printf("Client usage: chatclient <desired_username> <server_ip>:<server_port>\n");
            return 0;
        }
        
        sscanf(argv[2], "%[^:]:%u", ipaddr, &port);
        if(port == 0)
                port = DEFAULT_SERVER_PORT;

        client(ipaddr, port, argv[1]);
            
    }

#else
    #error "You must define one of SERVER_BUILD or CLIENT_BUILD!"
#endif