#ifndef _QL_EASY_FLASH_H_
#define _QL_EASY_FLASH_H_
#include "easyflash.h"
#include "uart_pub.h"

#ifdef __cplusplus
extern "C"
{
#endif

int ql_ef_get_env( void *value_buf,const char *key);


int ql_ef_set_env(const void *value_buf,const char *key,  size_t buf_len);


int ql_ef_del_env(const char *key);


#ifdef __cplusplus
}
#endif


#endif
