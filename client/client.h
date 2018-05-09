#ifndef _CLIENT_H_
#define _CLIENT_H_

#include "../common/common.h"


typedef struct {
    
    char username[USERNAME_LENG+1];
    UT_hash_handle hh;

} Member;



#define FILE_XFER_DIRECTORY "files_received"


#endif