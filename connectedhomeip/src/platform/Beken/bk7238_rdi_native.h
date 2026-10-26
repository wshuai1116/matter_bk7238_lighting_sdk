#ifndef BK7238_RDI_NATIVE_H
#define BK7238_RDI_NATIVE_H
#include "bk7238_rdi.h"
#include "sys_config.h"
#include "ble_api_5_x.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Targets Sparkleiot's legacy BK7238 read_req_t (value/size/length).
 * First boot requires a UID from factory data or Matter DRBG. Later boots
 * load the existing UID; no automatic replacement of malformed data. */
int bk_rdi_native_needs_uid(void);
int bk_rdi_native_begin(const uint8_t *first_uid, size_t first_uid_len);
int bk_rdi_native_copy(read_req_t *);
#ifdef __cplusplus
}
#endif
#endif
