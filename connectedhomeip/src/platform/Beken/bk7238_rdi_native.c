#include "bk7238_rdi_native.h"
#include "easyflash.h"
#include "mbedtls/sha256.h"
#include <string.h>
#include <stdio.h>

#define UID_KEY "rdi.uid.v1"
#define COUNTER_KEY "rdi.counter.v1"
#define STATE_KEY "rdi.state.v2"
/* One EasyFlash blob binds UID and counter. Never update legacy keys. */
#define RECORD_SIZE (9 + BK_RDI_UID_MAX)
static bk_rdi_state state;
static int read_state(uint8_t record[RECORD_SIZE])
{
    size_t size = 0;
    size_t copied = ef_get_env_blob(STATE_KEY, record, RECORD_SIZE, &size);
    if (copied != RECORD_SIZE || size != RECORD_SIZE ||
        memcmp(record, "RDI2", 4) || record[8] < BK_RDI_UID_MIN ||
        record[8] > BK_RDI_UID_MAX ||
        (uint8_t)(record[4] ^ record[6]) != 0xff ||
        (uint8_t)(record[5] ^ record[7]) != 0xff) return BK_RDI_STORAGE;
    return BK_RDI_OK;
}
static int store_state(uint8_t record[RECORD_SIZE], uint16_t counter)
{
    uint8_t check[RECORD_SIZE];
    record[4] = (uint8_t)counter; record[5] = (uint8_t)(counter >> 8);
    record[6] = (uint8_t)~record[4]; record[7] = (uint8_t)~record[5];
    EfErrCode result = ef_set_env_blob(STATE_KEY, record, RECORD_SIZE);
    if (result != EF_NO_ERR || read_state(check) != BK_RDI_OK ||
        memcmp(check, record, RECORD_SIZE)) {
        printf("RDI2: state write/readback failed result=%d\n", result);
        return BK_RDI_STORAGE;
    }
    return BK_RDI_OK;
}
static int read_legacy_counter(uint16_t *counter)
{
    uint8_t record[8]; size_t size = 0;
    if (ef_get_env_blob(COUNTER_KEY, record, 8, &size) != 8 || size != 8 ||
        memcmp(record, "RDI1", 4) ||
        (uint8_t)(record[4] ^ record[6]) != 0xff ||
        (uint8_t)(record[5] ^ record[7]) != 0xff) return BK_RDI_STORAGE;
    *counter = (uint16_t)(record[4] | (uint16_t)record[5] << 8);
    return BK_RDI_OK;
}
static int reserve_counter(void *context, uint16_t *counter)
{
    uint8_t record[RECORD_SIZE]; (void)context;
    if (read_state(record) != BK_RDI_OK) return BK_RDI_STORAGE;
    uint16_t previous = (uint16_t)(record[4] | (uint16_t)record[5] << 8);
    if (previous == UINT16_MAX) return BK_RDI_EXHAUSTED;
    *counter = previous + 1;
    return store_state(record, *counter);
}
static int hash(void *context, const uint8_t *data, size_t length, uint8_t out[32])
{ (void)context; return mbedtls_sha256_ret(data, length, out, 0); }

int bk_rdi_native_needs_uid(void)
{
    struct env_node_obj node;
    uint8_t record[RECORD_SIZE];
    if (ef_get_env_obj(STATE_KEY, &node)) {
        if (read_state(record) != BK_RDI_OK) {
            printf("RDI2: existing state invalid; refusing rollback\n");
            return BK_RDI_STORAGE;
        }
        return 0;
    }
    /* Migration requires the authoritative factory UID. */
    return 1;
}
int bk_rdi_native_begin(const uint8_t *uid, size_t length)
{
    bk_rdi_port port = {NULL, hash, reserve_counter};
    uint8_t record[RECORD_SIZE] = {0};
    struct env_node_obj node;
    uint16_t counter = 0;
    int result;
    bk_rdi_end(&state);
    if (ef_get_env_obj(STATE_KEY, &node)) {
        if (read_state(record) != BK_RDI_OK) return BK_RDI_STORAGE;
        counter = (uint16_t)(record[4] | (uint16_t)record[5] << 8);
        if (uid && (length != record[8] || memcmp(uid, record + 9, length))) {
            /* Keep the durable counter even when production changes UID. */
            result = bk_rdi_init(&state, uid, length, &port);
            if (result != BK_RDI_OK) return result;
            memset(record + 9, 0, BK_RDI_UID_MAX);
            memcpy(record + 9, uid, length); record[8] = (uint8_t)length;
            if (store_state(record, counter) != BK_RDI_OK) return BK_RDI_STORAGE;
        }
    } else {
        uint8_t previous_uid[BK_RDI_UID_MAX]; size_t size = 0;
        result = bk_rdi_init(&state, uid, length, &port);
        if (result != BK_RDI_OK) return result;
        int has_uid = ef_get_env_obj(UID_KEY, &node);
        int has_counter = ef_get_env_obj(COUNTER_KEY, &node);
        if (has_counter) {
            if (read_legacy_counter(&counter) != BK_RDI_OK) {
                printf("RDI2: legacy counter invalid; migration stopped\n");
                return BK_RDI_STORAGE;
            }
        } else if (has_uid) {
            size_t copied = ef_get_env_blob(UID_KEY, previous_uid, sizeof(previous_uid), &size);
            if (copied != size || size < BK_RDI_UID_MIN || size > BK_RDI_UID_MAX ||
                (size == length && !memcmp(previous_uid, uid, length))) {
                printf("RDI2: legacy counter missing, fresh cloud UID required\n");
                return BK_RDI_STORAGE;
            }
            printf("RDI2: new factory UID, replacing incomplete legacy identity\n");
        }
        memcpy(record, "RDI2", 4); record[8] = (uint8_t)length;
        memcpy(record + 9, uid, length);
        if (store_state(record, counter) != BK_RDI_OK) return BK_RDI_STORAGE;
        printf("RDI2: atomic state created, migrated_counter=%u\n", counter);
    }
    result = bk_rdi_init(&state, record + 9, record[8], &port);
    if (result == BK_RDI_OK) result = bk_rdi_begin(&state);
    printf("RDI2: advertising payload result=%d\n", result);
    return result;
}
int bk_rdi_native_copy(read_req_t *request)
{
    size_t written = 0; int result;
    if (!request) return BK_RDI_ARGUMENT;
    request->length = 0;
    if (!request->value || request->size < BK_RDI_TLV_SIZE) return BK_RDI_ARGUMENT;
    result = bk_rdi_read(&state, 0, request->value, request->size, &written);
    if (result == 0) request->length = (uint16_t)written;
    return result;
}
