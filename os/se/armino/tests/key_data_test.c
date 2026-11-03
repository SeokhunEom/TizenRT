/* Host regression for the HAL/TF-M descriptor boundary. */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "armino_key_data.h"

static hal_data *expected_key;
static hal_data *expected_private;
static int backend_status;

static void check_descriptor(const key25519_data_t *actual, const hal_data *expected)
{
    if (expected == NULL) {
        assert(actual == NULL);
        return;
    }
    assert(actual != NULL && (const void *)actual != (const void *)expected);
    assert(actual->data == expected->data && actual->data_len == expected->data_len);
    assert(actual->priv == expected->priv && actual->priv_len == expected->priv_len);
}

psa_status_t psa_sca_aes_set_key(aes_key_type type, uint32_t index, key_data_t *key)
{
    assert(type == AES_KEY_256 && index == 40);
    if (expected_key == NULL) {
        assert(key == NULL);
    } else {
        assert((void *)key != (void *)expected_key);
        assert(key->data == expected_key->data && key->data_len == expected_key->data_len);
        assert(key->priv == expected_key->priv && key->priv_len == expected_key->priv_len);
    }
    return backend_status;
}

int psa_25519_set_key(uint32_t index, key25519_data_t *key, key25519_data_t *prikey)
{
    assert(index == 40);
    check_descriptor(key, expected_key);
    check_descriptor(prikey, expected_private);
    return backend_status;
}

int psa_25519_get_key(uint32_t index, key25519_data_t *key)
{
    assert(index == 40);
    check_descriptor(key, expected_key);
    if (key != NULL) {
        memset(key->data, 0x25, 32);
        memset(key->priv, 0x19, 32);
        key->data_len = 32;
        key->priv_len = 32;
    }
    return backend_status;
}

int psa_25519_sign_hash(uint32_t index, key25519_data_t *hash, key25519_data_t *sign)
{
    assert(index == 40);
    check_descriptor(hash, expected_key);
    check_descriptor(sign, expected_private);
    if (sign != NULL) {
        memset(sign->data, 0xa5, 64);
        sign->data_len = 64;
        sign->priv_len = 0;
    }
    return backend_status;
}

int psa_25519_verify_hash(uint32_t index, key25519_data_t *hash, key25519_data_t *sign)
{
    assert(index == 40);
    check_descriptor(hash, expected_key);
    check_descriptor(sign, expected_private);
    return backend_status;
}

int main(void)
{
    uint8_t data[64] = {0}, private_data[64] = {0}, signature[80] = {0};
    hal_data public_key = {data, sizeof(data), private_data, sizeof(private_data)};
    hal_data private_key = {private_data, sizeof(private_data), NULL, 0};
    hal_data sign = {signature, sizeof(signature), NULL, 0};
    expected_key = &public_key;
    expected_private = &private_key;
    assert(armino_psa_sca_aes_set_key(AES_KEY_256, 40, &public_key) == 0);
    assert(armino_psa_25519_set_key(40, &public_key, &private_key) == 0);
    expected_key = NULL;
    assert(armino_psa_sca_aes_set_key(AES_KEY_256, 40, NULL) == 0);
    assert(armino_psa_25519_set_key(40, NULL, &private_key) == 0);
    expected_key = &public_key;
    expected_private = NULL;
    assert(armino_psa_25519_set_key(40, &public_key, NULL) == 0);

    assert(armino_psa_25519_get_key(40, &public_key) == 0);
    assert(public_key.data_len == 32 && public_key.priv_len == 32);
    assert(data[31] == 0x25 && private_data[31] == 0x19);
    expected_private = &sign;
    assert(armino_psa_25519_sign_hash(40, &public_key, &sign) == 0);
    assert(sign.data_len == 64 && sign.priv_len == 0 && signature[63] == 0xa5);
    assert(armino_psa_25519_verify_hash(40, &public_key, &sign) == 0);

    /* Preserve backend errors and length updates (including capacity reports). */
    backend_status = -123;
    sign.data_len = sizeof(signature);
    assert(armino_psa_25519_sign_hash(40, &public_key, &sign) == -123);
    assert(sign.data_len == 64);
    assert(armino_psa_25519_verify_hash(40, &public_key, &sign) == -123);
    assert(armino_psa_sca_aes_set_key(AES_KEY_256, 40, &public_key) == -123);
    expected_private = &private_key;
    assert(armino_psa_25519_set_key(40, &public_key, &private_key) == -123);
    assert(armino_psa_25519_get_key(40, &public_key) == -123);
    expected_key = expected_private = NULL;
    assert(armino_psa_25519_set_key(40, NULL, NULL) == -123);
    assert(armino_psa_25519_get_key(40, NULL) == -123);
    assert(armino_psa_25519_sign_hash(40, NULL, NULL) == -123);
    assert(armino_psa_25519_verify_hash(40, NULL, NULL) == -123);
    return 0;
}
