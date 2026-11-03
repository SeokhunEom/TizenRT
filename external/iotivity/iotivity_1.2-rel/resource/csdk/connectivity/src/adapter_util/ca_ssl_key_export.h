/* Copyright 2026 Samsung Electronics All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef CA_SSL_KEY_EXPORT_H
#define CA_SSL_KEY_EXPORT_H

#include <string.h>
#include "mbedtls/cipher.h"
#include "mbedtls/md.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/ssl.h"

/* Translate the Mbed TLS 3 master-secret event to IoTivity's TLS 1.2
 * key-block callback. The PRF seed is ServerHello.random || ClientHello.random.
 * AEAD suites use only the fixed IV bytes from the key block.
 */
static int CAExportTls12Keys(const mbedtls_ssl_session *session,
                            const unsigned char *secret, size_t secretLen,
                            const unsigned char clientRandom[32],
                            const unsigned char serverRandom[32],
                            mbedtls_tls_prf_types prf,
                            SslExportKeysCallback_t callback)
{
    if (!session || !secret || secretLen != 48 || !callback)
    {
        return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
    }

    const mbedtls_ssl_ciphersuite_t *suite =
        mbedtls_ssl_ciphersuite_from_id(session->MBEDTLS_PRIVATE(ciphersuite));
    const mbedtls_cipher_info_t *cipher = suite ?
        mbedtls_cipher_info_from_type(suite->MBEDTLS_PRIVATE(cipher)) : NULL;
    if (!cipher)
    {
        return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
    }

    const size_t keyLen = mbedtls_cipher_info_get_key_bitlen(cipher) / 8;
    size_t ivLen = mbedtls_cipher_info_get_iv_size(cipher);
    size_t macLen = 0;
    mbedtls_cipher_mode_t mode = mbedtls_cipher_info_get_mode(cipher);
    if (mode == MBEDTLS_MODE_GCM || mode == MBEDTLS_MODE_CCM)
    {
        ivLen = 4;
    }
    else if (mode == MBEDTLS_MODE_CHACHAPOLY)
    {
        ivLen = 12;
    }
    else
    {
        const mbedtls_md_info_t *md = mbedtls_md_info_from_type(suite->MBEDTLS_PRIVATE(mac));
        if (!md)
        {
            return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
        }
        macLen = mbedtls_md_get_size(md);
    }

    unsigned char keyBlock[256];
    unsigned char seed[64];
    const size_t keyBlockLen = 2 * (macLen + keyLen + ivLen);
    if (keyBlockLen > sizeof(keyBlock))
    {
        return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
    }
    memcpy(seed, serverRandom, 32);
    memcpy(seed + 32, clientRandom, 32);
    int result = mbedtls_ssl_tls_prf(prf, secret, secretLen, "key expansion",
                                   seed, sizeof(seed), keyBlock, keyBlockLen);
    if (result == 0)
    {
        callback(secret, keyBlock, macLen, keyLen, ivLen);
    }
    mbedtls_platform_zeroize(keyBlock, sizeof(keyBlock));
    return result;
}

#endif /* CA_SSL_KEY_EXPORT_H */
