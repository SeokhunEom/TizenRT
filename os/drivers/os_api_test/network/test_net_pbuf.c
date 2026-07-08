/****************************************************************************
 *
 * Copyright 2020 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>

#include <errno.h>
#include <stdint.h>
#include <sys/types.h>
#include <debug.h>
#include <string.h>

#include <tinyara/os_api_test_drv.h>

#include <lwip/pbuf.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

// #if !defined(CONFIG_TC_NET_PBUF) && defined(CONFIG_TC_KERNEL_NET_PBUF)
// struct pbuf_test_args {
// 	pbuf_layer layer;
// 	u16_t len;
// 	pbuf_type type;
// };
// #endif

/****************************************************************************
 * Private Definitions
 ****************************************************************************/

#define TEST_NET_PBUF_LEN		32
#define TEST_NET_PBUF_POOL_LEN		32
#define TEST_NET_PBUF_HEAD_LEN		12
#define TEST_NET_PBUF_TAIL_LEN		20

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int test_pbuf_is_valid_layer(pbuf_layer layer)
{
	switch (layer) {
	case PBUF_TRANSPORT:
	case PBUF_IP:
	case PBUF_LINK:
	case PBUF_RAW_TX:
	case PBUF_RAW:
		return 1;
	default:
		return 0;
	}
}

static int test_pbuf_is_valid_type(pbuf_type type)
{
	switch (type) {
	case PBUF_RAM:
	case PBUF_ROM:
	case PBUF_REF:
	case PBUF_POOL:
		return 1;
	default:
		return 0;
	}
}

static void test_pbuf_fill_pattern(uint8_t *buf, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++) {
		buf[i] = (uint8_t)(0xa0 + i);
	}
}

static int test_pbuf_ram(void)
{
	uint8_t pattern[TEST_NET_PBUF_LEN];
	uint8_t actual[TEST_NET_PBUF_LEN];
	struct pbuf *p;
	u16_t shrink_len;
	u8_t freed;

	test_pbuf_fill_pattern(pattern, sizeof(pattern));

	p = pbuf_alloc(PBUF_TRANSPORT, sizeof(pattern), PBUF_RAM);
	if (p == NULL) {
		dbg("pbuf_alloc PBUF_RAM failed.\n");
		return ERROR;
	}

	if (p->len != sizeof(pattern) || p->tot_len != sizeof(pattern) ||
			p->ref != 1 || pbuf_clen(p) != 1) {
		dbg("PBUF_RAM metadata is invalid.\n");
		goto errout_with_pbuf;
	}

	if (pbuf_take(p, pattern, sizeof(pattern)) != ERR_OK) {
		dbg("pbuf_take PBUF_RAM failed.\n");
		goto errout_with_pbuf;
	}

	if (pbuf_memcmp(p, 0, pattern, sizeof(pattern)) != 0) {
		dbg("pbuf_memcmp PBUF_RAM failed.\n");
		goto errout_with_pbuf;
	}

	memset(actual, 0, sizeof(actual));
	if (pbuf_copy_partial(p, actual, sizeof(actual), 0) != sizeof(actual) ||
			memcmp(pattern, actual, sizeof(actual)) != 0) {
		dbg("pbuf_copy_partial PBUF_RAM failed.\n");
		goto errout_with_pbuf;
	}

	if (pbuf_header(p, -PBUF_TRANSPORT_HLEN) != 0) {
		dbg("pbuf_header shrink failed.\n");
		goto errout_with_pbuf;
	}

	shrink_len = sizeof(pattern) - PBUF_TRANSPORT_HLEN;
	if (p->len != shrink_len || p->tot_len != shrink_len) {
		dbg("pbuf_header shrink metadata is invalid.\n");
		goto errout_with_pbuf;
	}

	if (pbuf_header(p, PBUF_TRANSPORT_HLEN) != 0) {
		dbg("pbuf_header restore failed.\n");
		goto errout_with_pbuf;
	}

	if (p->len != sizeof(pattern) || p->tot_len != sizeof(pattern)) {
		dbg("pbuf_header restore metadata is invalid.\n");
		goto errout_with_pbuf;
	}

	pbuf_ref(p);
	if (p->ref != 2) {
		dbg("pbuf_ref did not update refcount.\n");
		goto errout_with_pbuf;
	}

	freed = pbuf_free(p);
	if (freed != 0) {
		dbg("pbuf_free released referenced PBUF_RAM.\n");
		return ERROR;
	}

	if (p->ref != 1) {
		dbg("pbuf_free did not decrement PBUF_RAM refcount.\n");
		goto errout_with_pbuf;
	}

	freed = pbuf_free(p);
	if (freed != 1) {
		dbg("pbuf_free PBUF_RAM returned %u.\n", freed);
		return ERROR;
	}

	return OK;

errout_with_pbuf:
	pbuf_free(p);
	return ERROR;
}

static int test_pbuf_pool(void)
{
	uint8_t pattern[TEST_NET_PBUF_POOL_LEN];
	uint8_t actual[TEST_NET_PBUF_POOL_LEN];
	struct pbuf *p;
	u8_t freed;

	test_pbuf_fill_pattern(pattern, sizeof(pattern));

	p = pbuf_alloc(PBUF_RAW, sizeof(pattern), PBUF_POOL);
	if (p == NULL) {
		dbg("pbuf_alloc PBUF_POOL failed.\n");
		return ERROR;
	}

	if (p->tot_len != sizeof(pattern) || p->ref != 1) {
		dbg("PBUF_POOL metadata is invalid.\n");
		goto errout_with_pbuf;
	}

	if (pbuf_take(p, pattern, sizeof(pattern)) != ERR_OK) {
		dbg("pbuf_take PBUF_POOL failed.\n");
		goto errout_with_pbuf;
	}

	memset(actual, 0, sizeof(actual));
	if (pbuf_copy_partial(p, actual, sizeof(actual), 0) != sizeof(actual) ||
			memcmp(pattern, actual, sizeof(actual)) != 0) {
		dbg("pbuf_copy_partial PBUF_POOL failed.\n");
		goto errout_with_pbuf;
	}

	freed = pbuf_free(p);
	if (freed < 1) {
		dbg("pbuf_free PBUF_POOL returned %u.\n", freed);
		return ERROR;
	}

	return OK;

errout_with_pbuf:
	pbuf_free(p);
	return ERROR;
}

static int test_pbuf_chain(void)
{
	uint8_t pattern[TEST_NET_PBUF_HEAD_LEN + TEST_NET_PBUF_TAIL_LEN];
	uint8_t actual[TEST_NET_PBUF_HEAD_LEN + TEST_NET_PBUF_TAIL_LEN];
	struct pbuf *head;
	struct pbuf *tail;
	struct pbuf *remainder;
	u8_t freed;

	test_pbuf_fill_pattern(pattern, sizeof(pattern));

	head = pbuf_alloc(PBUF_RAW, TEST_NET_PBUF_HEAD_LEN, PBUF_RAM);
	if (head == NULL) {
		dbg("pbuf_alloc chain head failed.\n");
		return ERROR;
	}

	tail = pbuf_alloc(PBUF_RAW, TEST_NET_PBUF_TAIL_LEN, PBUF_RAM);
	if (tail == NULL) {
		dbg("pbuf_alloc chain tail failed.\n");
		pbuf_free(head);
		return ERROR;
	}

	if (pbuf_take(head, pattern, TEST_NET_PBUF_HEAD_LEN) != ERR_OK ||
		pbuf_take(tail, &pattern[TEST_NET_PBUF_HEAD_LEN], TEST_NET_PBUF_TAIL_LEN) != ERR_OK) {
		dbg("pbuf_take chain data failed.\n");
		goto errout_with_pbufs;
	}

	pbuf_chain(head, tail);
	if (head->next != tail || head->tot_len != sizeof(pattern) ||
		tail->tot_len != TEST_NET_PBUF_TAIL_LEN || tail->ref != 2 ||
		pbuf_clen(head) != 2) {
		dbg("pbuf_chain metadata is invalid.\n");
		goto errout_with_chain;
	}

	memset(actual, 0, sizeof(actual));
	if (pbuf_copy_partial(head, actual, sizeof(actual), 0) != sizeof(actual) ||
		memcmp(pattern, actual, sizeof(actual)) != 0) {
		dbg("pbuf_copy_partial chain failed.\n");
		goto errout_with_chain;
	}

	remainder = pbuf_dechain(head);
	if (remainder != tail || head->next != NULL ||
		head->tot_len != TEST_NET_PBUF_HEAD_LEN || tail->ref != 1) {
		dbg("pbuf_dechain metadata is invalid.\n");
		goto errout_with_pbufs;
	}

	freed = pbuf_free(head);
	if (freed != 1) {
		dbg("pbuf_free chain head returned %u.\n", freed);
		pbuf_free(tail);
		return ERROR;
	}

	freed = pbuf_free(tail);
	if (freed != 1) {
		dbg("pbuf_free chain tail returned %u.\n", freed);
		return ERROR;
	}

	return OK;

errout_with_chain:
	(void)pbuf_dechain(head);

errout_with_pbufs:
	pbuf_free(head);
	pbuf_free(tail);
	return ERROR;
}

static int test_pbuf_external_payload(pbuf_type type)
{
	uint8_t pattern[TEST_NET_PBUF_LEN];
	uint8_t actual[TEST_NET_PBUF_LEN];
	struct pbuf *p;
	u8_t freed;

	test_pbuf_fill_pattern(pattern, sizeof(pattern));

	p = pbuf_alloc(PBUF_RAW, sizeof(pattern), type);
	if (p == NULL) {
		dbg("pbuf_alloc external payload failed.\n");
		return ERROR;
	}

	if (p->payload != NULL || p->len != sizeof(pattern) ||
		p->tot_len != sizeof(pattern) || p->ref != 1 || p->type != type) {
		dbg("external payload pbuf metadata is invalid.\n");
		goto errout_with_pbuf;
	}

	p->payload = pattern;
	if (pbuf_memcmp(p, 0, pattern, sizeof(pattern)) != 0) {
		dbg("pbuf_memcmp external payload failed.\n");
		goto errout_with_pbuf;
	}

	memset(actual, 0, sizeof(actual));
	if (pbuf_copy_partial(p, actual, sizeof(actual), 0) != sizeof(actual) ||
		memcmp(pattern, actual, sizeof(actual)) != 0) {
		dbg("pbuf_copy_partial external payload failed.\n");
		goto errout_with_pbuf;
	}

	if (pbuf_take(p, pattern, sizeof(pattern) + 1) != ERR_MEM) {
		dbg("pbuf_take accepted oversized data.\n");
		goto errout_with_pbuf;
	}

	freed = pbuf_free(p);
	if (freed != 1) {
		dbg("pbuf_free external payload returned %u.\n", freed);
		return ERROR;
	}

	return OK;

errout_with_pbuf:
	pbuf_free(p);
	return ERROR;
}

static int test_pbuf_reference_types(void)
{
	if (test_pbuf_external_payload(PBUF_ROM) != OK) {
		return ERROR;
	}

	return test_pbuf_external_payload(PBUF_REF);
}

static int test_pbuf_kernel_suite(void)
{
	if (test_pbuf_ram() != OK) {
		return ERROR;
	}

	if (test_pbuf_pool() != OK) {
		return ERROR;
	}

	if (test_pbuf_chain() != OK) {
		return ERROR;
	}

	if (test_pbuf_reference_types() != OK) {
		return ERROR;
	}

	return OK;
}

static int test_pbuf_legacy(unsigned long arg)
{
	struct pbuf *p;
	FAR struct pbuf_test_args *args = (FAR struct pbuf_test_args *)arg;

	if (args == NULL ||
			!test_pbuf_is_valid_layer(args->layer) ||
			!test_pbuf_is_valid_type(args->type)) {
		return 0;
	}

	p = pbuf_alloc(args->layer, args->len, args->type);
	if (p == NULL) {
		return 0;
	}

	pbuf_free(p);
	return 1;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int test_net_pbuf(int cmd, unsigned long arg)
{
	switch (cmd) {
	case TESTIOC_NET_PBUF:
		if (arg == 0) {
			return test_pbuf_kernel_suite();
		}

		return test_pbuf_legacy(arg);
	}

	return -EINVAL;
}
