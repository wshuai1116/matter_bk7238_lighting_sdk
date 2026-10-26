#include "bk7238_rdi.h"
#include <string.h>

static void wipe(void *p, size_t n)
{
    volatile uint8_t *b = (volatile uint8_t *)p;
    while (n--) *b++ = 0;
}
static int valid_uid(const uint8_t *uid, size_t len)
{
    size_t i;
    uint8_t any = 0, not_ff = 0;
    if (!uid || len < BK_RDI_UID_MIN || len > BK_RDI_UID_MAX) return 0;
    for (i = 0; i < len; ++i) { any |= uid[i]; not_ff |= (uint8_t)~uid[i]; }
    return any != 0 && not_ff != 0;
}
int bk_rdi_init(bk_rdi_state *s, const uint8_t *uid, size_t len, const bk_rdi_port *p)
{
    if (!s) return BK_RDI_ARGUMENT;
    /* uid and p must not alias s. */
    wipe(s, sizeof(*s));
    if (!p || !p->hash || !p->reserve || !valid_uid(uid, len)) return BK_RDI_ARGUMENT;
    s->port = *p;
    memcpy(s->uid, uid, len);
    s->uid_len = len;
    s->initialized = 1;
    return BK_RDI_OK;
}
int bk_rdi_encode(const uint8_t *uid, size_t len, uint16_t counter,
                  bk_rdi_hash_fn hash, void *context, uint8_t out[BK_RDI_TLV_SIZE])
{
    uint8_t input[BK_RDI_UID_MAX + 2], digest[32];
    int result;
    if (!out) return BK_RDI_ARGUMENT;
    memset(out, 0, BK_RDI_TLV_SIZE);
    if (!hash || !valid_uid(uid, len)) return BK_RDI_ARGUMENT;
    memcpy(input, uid, len);
    input[len] = (uint8_t)counter;
    input[len + 1] = (uint8_t)(counter >> 8);
    result = hash(context, input, len + 2, digest);
    wipe(input, sizeof(input));
    if (result != 0) { wipe(digest, sizeof(digest)); return BK_RDI_CRYPTO; }
    /* Anonymous structure; context-tagged byte string (tag 0, length 18);
     * counter LE + last 16 bytes of SHA256(uid || counter LE); end container. */
    out[0] = 0x15; out[1] = 0x30; out[2] = 0; out[3] = BK_RDI_SIZE;
    out[4] = (uint8_t)counter; out[5] = (uint8_t)(counter >> 8);
    memcpy(out + 6, digest + 16, 16);
    out[22] = 0x18;
    wipe(digest, sizeof(digest));
    return BK_RDI_OK;
}
void bk_rdi_end(bk_rdi_state *s)
{
    if (s) { s->ready = 0; wipe(s->payload, sizeof(s->payload)); }
}
int bk_rdi_begin(bk_rdi_state *s)
{
    uint16_t counter;
    int result;
    if (!s) return BK_RDI_ARGUMENT;
    bk_rdi_end(s);
    if (!s->initialized) return BK_RDI_NOT_READY;
    result = s->port.reserve(s->port.context, &counter);
    if (result != 0) return result == BK_RDI_EXHAUSTED ? result : BK_RDI_STORAGE;
    /* A failed hash may consume a counter. Gaps are preferable to reuse. */
    result = bk_rdi_encode(s->uid, s->uid_len, counter, s->port.hash,
                           s->port.context, s->payload);
    if (result != 0) return result;
    s->counter = counter;
    s->ready = 1;
    return BK_RDI_OK;
}
int bk_rdi_read(const bk_rdi_state *s, size_t offset, uint8_t *out,
                size_t capacity, size_t *written)
{
    size_t len;
    if (!written) return BK_RDI_ARGUMENT;
    *written = 0;
    if (!s || (!out && capacity)) return BK_RDI_ARGUMENT;
    if (!s->ready) return BK_RDI_NOT_READY;
    if (offset > BK_RDI_TLV_SIZE) return BK_RDI_OFFSET;
    len = BK_RDI_TLV_SIZE - offset;
    if (len > capacity) len = capacity;
    if (len) memcpy(out, s->payload + offset, len);
    *written = len;
    return BK_RDI_OK;
}
int bk_rdi_set_advertisement_flag(const bk_rdi_state *s, uint8_t *data, size_t len)
{
    if (!s || !data || len != 8) return BK_RDI_ARGUMENT;
    data[7] = (uint8_t)((data[7] & 0xFEu) | (s->ready ? 1u : 0u));
    return BK_RDI_OK;
}
