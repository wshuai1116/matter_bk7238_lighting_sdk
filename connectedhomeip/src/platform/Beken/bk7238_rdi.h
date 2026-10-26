#ifndef BK7238_RDI_H
#define BK7238_RDI_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define BK_RDI_UID_MIN 16u
#define BK_RDI_UID_MAX 128u
#define BK_RDI_SIZE 18u
#define BK_RDI_TLV_SIZE 23u
enum { BK_RDI_OK = 0, BK_RDI_ARGUMENT = -1, BK_RDI_STORAGE = -2,
       BK_RDI_CRYPTO = -3, BK_RDI_NOT_READY = -4, BK_RDI_OFFSET = -5,
       BK_RDI_EXHAUSTED = -6 };

/* SHA-256 over the exact input, output exactly 32 bytes; return 0 on success. */
typedef int (*bk_rdi_hash_fn)(void *, const uint8_t *, size_t, uint8_t[32]);
/* Reserve a strictly increasing uint16 counter DURABLY before returning 0.
 * Never roll back after uncertain writes; reload durable state on each call.
 * Missing/corrupt state is an error, not permission to reset to zero.
 * Refuse at 65535; a fresh ID/provisioning policy is needed for further rotation. */
typedef int (*bk_rdi_reserve_fn)(void *, uint16_t *);
typedef struct {
    void *context;
    bk_rdi_hash_fn hash;
    bk_rdi_reserve_fn reserve;
} bk_rdi_port;
typedef struct {
    bk_rdi_port port;
    uint8_t uid[BK_RDI_UID_MAX];
    size_t uid_len;
    uint8_t payload[BK_RDI_TLV_SIZE];
    uint16_t counter;
    uint8_t initialized;
    uint8_t ready;
} bk_rdi_state;

/* Single owner/event loop only: serialize init/begin/read/end and provisioning. */
int bk_rdi_init(bk_rdi_state *, const uint8_t *, size_t, const bk_rdi_port *);
/* Deterministic encoder useful for official test vectors. Does not persist. */
int bk_rdi_encode(const uint8_t *, size_t, uint16_t, bk_rdi_hash_fn, void *,
                  uint8_t out[BK_RDI_TLV_SIZE]);
/* Call before commencing a new advertising session, not for each GATT read,
 * advertising packet, or fast/slow interval switch. Failure disables reads. */
int bk_rdi_begin(bk_rdi_state *);
void bk_rdi_end(bk_rdi_state *);
int bk_rdi_read(const bk_rdi_state *, size_t offset, uint8_t *out,
                size_t capacity, size_t *written);
/* Input is the 8-byte Matter Device Identification block, WITHOUT AD header
 * or service UUID. Only modify its Additional Data flag (byte 7, bit 0). */
int bk_rdi_set_advertisement_flag(const bk_rdi_state *, uint8_t *, size_t);
#ifdef __cplusplus
}
#endif
#endif
