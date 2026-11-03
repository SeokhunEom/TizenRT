#!/usr/bin/env python3
"""Exercise the real DH alternate glue with a host SecLink/MPI test double.

This checks pointer contracts and cleanup, not cryptographic implementation.
Run with Python 3 and a C compiler; an optional source path supports reproducing
the regression against a previous revision of dhm_alt.c.
"""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


CONTRACT = r"""
#ifndef DHM_TEST_CONTRACT_H
#define DHM_TEST_CONTRACT_H
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define MBEDTLS_DHM_C
#define MBEDTLS_DHM_ALT
#define DHM_1024 128
#define DHM_2048 256
#define DHM_4096 512
#define MBEDTLS_ERR_DHM_BAD_INPUT_DATA -100
#define MBEDTLS_ERR_DHM_INVALID_FORMAT -200
#define MBEDTLS_ERR_DHM_ALLOC_FAILED -300
#define MBEDTLS_ERR_DHM_FILE_IO_ERROR -400
#define MBEDTLS_ERR_DHM_HW_ACCEL_FAILED -500
#define MBEDTLS_ERR_DHM_MAKE_PUBLIC_FAILED -600
#define MBEDTLS_DHM_KEY_INDEX_ALT 1
#define MBEDTLS_BYTE_0(x) ((unsigned char)(x))
#define MBEDTLS_BYTE_1(x) ((unsigned char)((x) >> 8))
#define MBEDTLS_MPI_CHK(expr) do { if ((ret = (expr)) != 0) goto cleanup; } while (0)
typedef struct { size_t size; int value; } mbedtls_mpi;
typedef struct {
    size_t len;
    mbedtls_mpi P, G, GX, GY, K;
    unsigned int key_index;
} mbedtls_dhm_context;
typedef struct { unsigned char *data; unsigned int data_len; void *priv; unsigned int priv_len; } hal_data;
enum { HAL_DH_UNKNOWN, HAL_DH_1024, HAL_DH_2048, HAL_DH_4096 };
typedef struct { int mode; hal_data *G, *P, *pubkey; } hal_dh_data;
typedef void *sl_ctx;
#define SECLINK_OK 0
static int live_allocations, allocation_count, fail_allocation;
static int init_result, operation_result, write_result, deinit_count;
static void *checked_malloc(size_t size)
{
    if (++allocation_count == fail_allocation) return NULL;
    void *result = malloc(size);
    if (result) ++live_allocations;
    return result;
}
static void checked_free(void *ptr)
{
    if (ptr) --live_allocations;
    free(ptr);
}
static void mbedtls_mpi_init(mbedtls_mpi *v) { memset(v, 0, sizeof(*v)); }
static void mbedtls_mpi_free(mbedtls_mpi *v) { (void)v; }
static int mbedtls_mpi_sub_int(mbedtls_mpi *r, const mbedtls_mpi *v, int x)
{ r->value = v->value - x; return 0; }
static int mbedtls_mpi_cmp_int(const mbedtls_mpi *v, int x)
{ return (v->value > x) - (v->value < x); }
static int mbedtls_mpi_cmp_mpi(const mbedtls_mpi *v, const mbedtls_mpi *x)
{ return mbedtls_mpi_cmp_int(v, x->value); }
static size_t mbedtls_mpi_size(const mbedtls_mpi *v) { return v->size; }
static size_t mbedtls_dhm_get_len(const mbedtls_dhm_context *ctx) { return ctx->P.size; }
static int mbedtls_mpi_write_binary(const mbedtls_mpi *v, unsigned char *p, size_t len)
{
    if (write_result) return write_result;
    assert(p != NULL && len >= v->size);
    memset(p, v->value, len);
    return 0;
}
static int mbedtls_mpi_read_binary(mbedtls_mpi *v, const unsigned char *p, size_t len)
{ assert(p != NULL); v->size = len; v->value = 3; return 0; }
static int sl_init(sl_ctx *ctx)
{
    assert(ctx != NULL);
    if (init_result) return init_result;
    *ctx = (void *)(uintptr_t)1;
    return 0;
}
static int sl_deinit(sl_ctx ctx)
{ assert(ctx == (void *)(uintptr_t)1); ++deinit_count; return 0; }
static int sl_dh_generate_param(sl_ctx ctx, unsigned int index, hal_dh_data *params)
{
    assert(ctx == (void *)(uintptr_t)1 && index == MBEDTLS_DHM_KEY_INDEX_ALT);
    assert(params->mode == HAL_DH_2048);
    assert(params->P && params->G && params->pubkey);
    assert(params->P->data_len == 256 && params->G->data_len == 1);
    assert(params->pubkey->data_len == 256);
    assert(params->P->data && params->G->data && params->pubkey->data);
    memset(params->pubkey->data, 3, params->pubkey->data_len);
    return operation_result;
}
static int sl_dh_compute_shared_secret(sl_ctx ctx, hal_dh_data *params,
                                      unsigned int index, hal_data *secret)
{
    assert(ctx == (void *)(uintptr_t)1 && index == MBEDTLS_DHM_KEY_INDEX_ALT);
    assert(params->P && params->G && params->pubkey);
    assert(params->pubkey->data_len == 128 && params->pubkey->data);
    assert(secret->data && secret->data_len == 256);
    if (operation_result) return operation_result;
    secret->data[0] = 4;
    secret->data_len = 1;
    return 0;
}
#endif
"""

CASES = r"""
static mbedtls_dhm_context context(void)
{
    mbedtls_dhm_context ctx;
    mbedtls_dhm_init(&ctx);
    ctx.P.size = 256; ctx.P.value = 23;
    ctx.G.size = 1; ctx.G.value = 2;
    ctx.GY.size = 128; ctx.GY.value = 3;
    return ctx;
}
static void reset(void)
{
    assert(live_allocations == 0);
    allocation_count = fail_allocation = init_result = operation_result = write_result = deinit_count = 0;
}
int main(void)
{
    unsigned char output[1024];
    size_t length = 0;
    mbedtls_dhm_context ctx = context();
    reset();
    assert(mbedtls_dhm_make_params(&ctx, 128, output, &length, NULL, NULL) == 0);
    assert(length == 256 + 1 + 256 + 6 && ctx.len == 256 && deinit_count == 1);
    reset();
    assert(mbedtls_dhm_make_public(&ctx, 128, output, 256, NULL, NULL) == 0);
    assert(deinit_count == 1);
    reset();
    assert(mbedtls_dhm_calc_secret(&ctx, output, 256, &length, NULL, NULL) == 0);
    assert(length == 1 && output[0] == 4 && deinit_count == 1);
    for (int failure = 1; failure <= 3; ++failure) {
        reset(); fail_allocation = failure;
        assert(mbedtls_dhm_make_public(&ctx, 128, output, 256, NULL, NULL) ==
               MBEDTLS_ERR_DHM_MAKE_PUBLIC_FAILED + MBEDTLS_ERR_DHM_ALLOC_FAILED);
        assert(deinit_count == 1);
    }
    reset(); init_result = -1;
    assert(mbedtls_dhm_make_public(&ctx, 128, output, 256, NULL, NULL) ==
           MBEDTLS_ERR_DHM_MAKE_PUBLIC_FAILED + MBEDTLS_ERR_DHM_FILE_IO_ERROR);
    assert(deinit_count == 0 && allocation_count == 0);
    reset(); operation_result = -1;
    assert(mbedtls_dhm_make_public(&ctx, 128, output, 256, NULL, NULL) ==
           MBEDTLS_ERR_DHM_MAKE_PUBLIC_FAILED + MBEDTLS_ERR_DHM_HW_ACCEL_FAILED);
    assert(deinit_count == 1);
    reset(); write_result = -77;
    assert(mbedtls_dhm_make_public(&ctx, 128, output, 256, NULL, NULL) ==
           MBEDTLS_ERR_DHM_MAKE_PUBLIC_FAILED - 77);
    assert(deinit_count == 1);
    reset();
    return 0;
}
"""


def main():
    source = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1] / "alt/dhm_alt.c"
    with tempfile.TemporaryDirectory(prefix="dhm-alt-test-") as scratch:
        root = Path(scratch)
        for name in ("tinyara/config.h", "tinyara/seclink.h", "tinyara/security_hal.h",
                     "mbedtls/common.h", "mbedtls/dhm.h", "mbedtls/platform_util.h",
                     "mbedtls/error.h", "mbedtls/platform.h", "mbedtls/alt/dhm_alt.h",
                     "mbedtls/alt/common.h"):
            header = root / name
            header.parent.mkdir(parents=True, exist_ok=True)
            header.write_text('#include "contract.h"\n')
        (root / "contract.h").write_text(CONTRACT)
        test = root / "test.c"
        test.write_text('#include "contract.h"\n#define malloc checked_malloc\n'
                        '#define free checked_free\n' + source.read_text() + CASES)
        binary = root / "test"
        subprocess.run([os.environ.get("CC", "cc"), "-std=c99", "-Wall", "-Werror",
                        "-I", str(root), str(test), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("DHM alternate pointer, capacity, error propagation and cleanup checks passed")


if __name__ == "__main__":
    main()
