#ifndef __ATSVR_UTIL_H__
#define __ATSVR_UTIL_H__
#include "stdbool.h"
#include "stdlib.h"
#include "stdio.h"
#ifdef __cplusplus
extern "C"
{
#endif

#define AT_CMD_NONE 0x00
#define AT_CMD_NA   0x01
#define AT_CMD_EQ   0x02
#define AT_CMD_QU   0x04
#define AT_CMD_AR   0x08

#define ATSVR_ESC_CHAR        '+'

extern unsigned char atsvr_s3s4[];
#define MT_AT_RESPONSE_ERROR   do{\
    atsvr_cmd_rsp_error();\
    bk_printf("--------------->%s:%d\r\n",__FUNCTION__ ,__LINE__);\
    return ;}while(0);


bool atsvr_check_atcmd_type(int argc, char **argv, int *atcmd_type);

bool atsvr_strip_out_string(char *argv, char *out, int out_len);

bool atsvr_atoi(char *argv, int *num, int base);

bool atsvr_hex_to_string(char *hex_data, int data_len, char *string);

bool atsvr_hex_to_string_ble(char *hex_data, int data_len, char *string);

bool atsvr_string_to_hex(char *input , int input_len, char *output);

extern unsigned short generate_discriminator_by_mac();

#ifdef __cplusplus
}
#endif
#endif

