/****************************************************************************
 *
 * Copyright 2026 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ****************************************************************************/

#ifndef __SE_ARMINO_KEY_DATA_H
#define __SE_ARMINO_KEY_DATA_H

#include <stddef.h>
#include <tinyara/security_hal.h>
#include "tfm_25519_nsc.h"
#include "tfm_sca_aes_nsc.h"

/* HAL and TF-M descriptors have distinct C types. Copy their fields rather
 * than accessing a HAL object through an incompatible TF-M struct pointer.
 * Only descriptors are copied: data buffers retain their original ownership.
 */
static inline key25519_data_t armino_key25519_data(const hal_data *data)
{
    key25519_data_t result = {0};
    if (data != NULL) {
        result.data = data->data;
        result.data_len = data->data_len;
        result.priv = data->priv;
        result.priv_len = data->priv_len;
    }
    return result;
}

static inline void armino_update_hal_data(hal_data *data, const key25519_data_t *result)
{
    if (data != NULL) {
        data->data = result->data;
        data->data_len = result->data_len;
        data->priv = result->priv;
        data->priv_len = result->priv_len;
    }
}

static inline psa_status_t armino_psa_sca_aes_set_key(aes_key_type type, uint32_t index,
                                                     hal_data *key)
{
    key_data_t converted = {0};
    if (key != NULL) {
        converted.data = key->data;
        converted.data_len = key->data_len;
        converted.priv = key->priv;
        converted.priv_len = key->priv_len;
    }
    psa_status_t ret = psa_sca_aes_set_key(type, index, key != NULL ? &converted : NULL);
    if (key != NULL) {
        key->data = converted.data;
        key->data_len = converted.data_len;
        key->priv = converted.priv;
        key->priv_len = converted.priv_len;
    }
    return ret;
}

static inline int armino_psa_25519_set_key(uint32_t index, hal_data *key, hal_data *prikey)
{
    key25519_data_t public_key = armino_key25519_data(key);
    key25519_data_t private_key = armino_key25519_data(prikey);
    int ret = psa_25519_set_key(index, key != NULL ? &public_key : NULL,
                               prikey != NULL ? &private_key : NULL);
    armino_update_hal_data(key, &public_key);
    armino_update_hal_data(prikey, &private_key);
    return ret;
}

static inline int armino_psa_25519_get_key(uint32_t index, hal_data *key)
{
    key25519_data_t converted = armino_key25519_data(key);
    int ret = psa_25519_get_key(index, key != NULL ? &converted : NULL);
    armino_update_hal_data(key, &converted);
    return ret;
}

static inline int armino_psa_25519_sign_hash(uint32_t index, hal_data *hash, hal_data *sign)
{
    key25519_data_t hash_data = armino_key25519_data(hash);
    key25519_data_t sign_data = armino_key25519_data(sign);
    int ret = psa_25519_sign_hash(index, hash != NULL ? &hash_data : NULL,
                                 sign != NULL ? &sign_data : NULL);
    armino_update_hal_data(hash, &hash_data);
    armino_update_hal_data(sign, &sign_data);
    return ret;
}

static inline int armino_psa_25519_verify_hash(uint32_t index, hal_data *hash, hal_data *sign)
{
    key25519_data_t hash_data = armino_key25519_data(hash);
    key25519_data_t sign_data = armino_key25519_data(sign);
    int ret = psa_25519_verify_hash(index, hash != NULL ? &hash_data : NULL,
                                   sign != NULL ? &sign_data : NULL);
    armino_update_hal_data(hash, &hash_data);
    armino_update_hal_data(sign, &sign_data);
    return ret;
}

#endif /* __SE_ARMINO_KEY_DATA_H */
