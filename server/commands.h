#ifndef _SERVER_COMMANDS_H_
#define _SERVER_COMMANDS_H_

#include "../common/common.h"



int parse_client_command();
int handle_admin_commands(char *buffer);


#endif