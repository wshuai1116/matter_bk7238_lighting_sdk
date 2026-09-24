/*
 *
 *    Copyright (c) 2022 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

/**
 *    @file
 *          Platform-specific key value storage implementation for Beken
 */
/* this file behaves like a config.h, comes first */
#include "easyflash.h"
#include "ef_def.h"
#include "matter_pal.h"
#include <platform/KeyValueStoreManager.h>
#include <lib/support/CodeUtils.h>

namespace chip {
namespace DeviceLayer {
namespace PersistedStorage {

KeyValueStoreManagerImpl KeyValueStoreManagerImpl::sInstance;

CHIP_ERROR KeyValueStoreManagerImpl::_Get(const char * key, void * value, size_t value_size, size_t * read_bytes_size,
                                          size_t offset_bytes)
{
    CHIP_ERROR err         = CHIP_NO_ERROR;
    uint32_t dwTeReadBytes = 0;

    if ((!value) || offset_bytes > 0)
    {
        // Offset and partial reads are not supported in nvs, for now just return NOT_IMPLEMENTED. Support can be added in the
        // future if this is needed.
        return (err = CHIP_ERROR_INVALID_ARGUMENT);
    }

    dwTeReadBytes = ef_get_env_blob(key, value, value_size, read_bytes_size);
    if (read_bytes_size != NULL)
    {
        *read_bytes_size = dwTeReadBytes;
    }
    if (0 != dwTeReadBytes)
    {
        err = CHIP_NO_ERROR;
    }
    else
    {
        err = CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND;
    }

    return err;
}

CHIP_ERROR KeyValueStoreManagerImpl::_Put(const char * key, const void * value, size_t value_size)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    if (!value)
    {
        return (err = CHIP_ERROR_INVALID_ARGUMENT);
    }

    EfErrCode ret = ef_set_env_blob(key, value, value_size);

    if (ret == EF_NO_ERR)
    {
        err = CHIP_NO_ERROR;
    }
    else
    {
        err = CHIP_ERROR_INTERNAL;
    }

    return err;
}

CHIP_ERROR KeyValueStoreManagerImpl::_Delete(const char * key)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    EfErrCode ret = ef_set_env_blob(key, NULL, 0);

    if (ret == EF_NO_ERR)
    {
        err = CHIP_NO_ERROR;
    }
    else
    {
        err = CHIP_ERROR_INTERNAL;
    }

    return CHIP_NO_ERROR;
}

} // namespace PersistedStorage
} // namespace DeviceLayer
} // namespace chip
