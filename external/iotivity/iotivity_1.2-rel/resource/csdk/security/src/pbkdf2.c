/******************************************************************
*
* Copyright 2016 Microsoft Corporation
*
*
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
******************************************************************/

#include "logger.h"
#include "mbedtls/pkcs5.h"
#include "mbedtls/md.h"

#define TAG "OIC_SEC_PBDKF2"

int DeriveCryptoKeyFromPassword(const unsigned char *passwd, size_t pLen,
    const uint8_t *salt, const size_t saltLen,
    const size_t iterations,
    const size_t keyLen, uint8_t *derivedKey)
{
    int ret = mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, passwd, pLen,
                                            salt, saltLen, iterations, keyLen, derivedKey);
    if (ret != 0)
    {
        OIC_LOG(ERROR, TAG, "Call to mbedtls PBKDF2 function failed");
    }

    return ret;
}
