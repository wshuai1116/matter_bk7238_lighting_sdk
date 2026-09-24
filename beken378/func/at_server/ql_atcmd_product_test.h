#ifndef _ATCMD_PRODUCT_H_
#define _ATCMD_PRODUCT_H_

#define QUEC_PRODUCT_INFO_SN_LENGTH 64
#define MAC_ADDR_LEN  6

typedef struct _user_flash__
{
    unsigned char sn[QUEC_PRODUCT_INFO_SN_LENGTH];
    unsigned char mac[MAC_ADDR_LEN];
}user_flash_t;

void ql_write_flash(user_flash_t *user_info,unsigned int len);


void ql_read_flash(user_flash_t *user_info,unsigned int len);



#endif