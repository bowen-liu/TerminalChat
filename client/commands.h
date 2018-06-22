#ifndef _CLIENT_COMMANDS_H_
#define _CLIENT_COMMANDS_H_

#include "../common/common.h"

int handle_user_command();
void parse_error_code();
void parse_control_message(char* cmd_buffer);


#endif