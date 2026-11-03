#include <assert.h>
#include <string.h>
#include "mbedtls/ssl.h"
#include "mbedtls/cipher.h"

typedef void (*SslExportKeysCallback_t)(const unsigned char *, const unsigned char *,
                                      size_t, size_t, size_t);
#include "ca_ssl_key_export.h"

static mbedtls_ssl_ciphersuite_t suite;
static mbedtls_cipher_info_t cipher;
static unsigned char master[48], client[32], server[32];
static size_t expected_mac, expected_key, expected_iv;
static int callbacks, prf_result, zeroized;

const mbedtls_ssl_ciphersuite_t *mbedtls_ssl_ciphersuite_from_id(int id)
{
    return id == 1 ? &suite : NULL;
}

const mbedtls_cipher_info_t *mbedtls_cipher_info_from_type(mbedtls_cipher_type_t type)
{
    (void)type;
    return &cipher;
}

const mbedtls_md_info_t *mbedtls_md_info_from_type(mbedtls_md_type_t type)
{
    (void)type;
    return (const mbedtls_md_info_t *)&suite;
}

unsigned char mbedtls_md_get_size(const mbedtls_md_info_t *info)
{
    (void)info;
    return (unsigned char)expected_mac;
}

int mbedtls_ssl_tls_prf(mbedtls_tls_prf_types prf, const unsigned char *secret,
                      size_t slen, const char *label, const unsigned char *random,
                      size_t rlen, unsigned char *output, size_t dlen)
{
    assert(prf == MBEDTLS_SSL_TLS_PRF_SHA256);
    assert(slen == sizeof(master) && memcmp(secret, master, slen) == 0);
    assert(strcmp(label, "key expansion") == 0);
    assert(rlen == 64);
    assert(memcmp(random, server, 32) == 0);
    assert(memcmp(random + 32, client, 32) == 0);
    assert(dlen == 2 * (expected_mac + expected_key + expected_iv));
    memset(output, 0xa5, dlen);
    return prf_result;
}

void mbedtls_platform_zeroize(void *buffer, size_t size)
{
    memset(buffer, 0, size);
    ++zeroized;
}

static void exported(const unsigned char *secret, const unsigned char *block,
                     size_t mac, size_t key, size_t iv)
{
    assert(secret == master);
    assert(mac == expected_mac && key == expected_key && iv == expected_iv);
    for (size_t i = 0; i < 2 * (mac + key + iv); ++i)
    {
        assert(block[i] == 0xa5);
    }
    ++callbacks;
}

int main(void)
{
    mbedtls_ssl_session session = {0};
    session.MBEDTLS_PRIVATE(ciphersuite) = 1;
    memset(master, 0x11, sizeof(master));
    memset(client, 0x22, sizeof(client));
    memset(server, 0x33, sizeof(server));
    cipher.MBEDTLS_PRIVATE(iv_size) = 16;
    const mbedtls_cipher_mode_t modes[] = {
        MBEDTLS_MODE_CBC, MBEDTLS_MODE_GCM, MBEDTLS_MODE_CCM,
        MBEDTLS_MODE_CHACHAPOLY, MBEDTLS_MODE_CBC
    };
    const size_t mac_lengths[] = {32, 0, 0, 0, 48};
    const size_t key_lengths[] = {16, 16, 16, 32, 32};
    const size_t iv_lengths[] = {16, 4, 4, 12, 16};
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i)
    {
        cipher.MBEDTLS_PRIVATE(mode) = modes[i];
        expected_mac = mac_lengths[i];
        expected_key = key_lengths[i];
        expected_iv = iv_lengths[i];
        cipher.MBEDTLS_PRIVATE(key_bitlen) = expected_key * 8;
        assert(CAExportTls12Keys(&session, master, 48, client, server,
                                MBEDTLS_SSL_TLS_PRF_SHA256, exported) == 0);
    }
    assert(callbacks == 5 && zeroized == 5);
    prf_result = -1;
    assert(CAExportTls12Keys(&session, master, 48, client, server,
                            MBEDTLS_SSL_TLS_PRF_SHA256, exported) == -1);
    assert(callbacks == 5 && zeroized == 6);
    assert(CAExportTls12Keys(&session, master, 47, client, server,
                            MBEDTLS_SSL_TLS_PRF_SHA256, exported) != 0);
    session.MBEDTLS_PRIVATE(ciphersuite) = 0;
    assert(CAExportTls12Keys(&session, master, 48, client, server,
                            MBEDTLS_SSL_TLS_PRF_SHA256, exported) != 0);
    assert(callbacks == 5);
    return 0;
}
