#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include <lwip/pbuf.h>

#if defined(PBUF_RED_PRIVATE_ABI) && !defined(CONFIG_TC_NET_PBUF)
struct pbuf_test_args {
	pbuf_layer layer;
	u16_t len;
	pbuf_type type;
};
#endif

#include PRODUCT_SOURCE

int main(void)
{
	unsigned char opaque_arg = 0;
	unsigned long nonzero_arg = (unsigned long)&opaque_arg;
	unsigned int zero_allocs;
	unsigned int nonzero_allocs;
	int zero_result;
	int nonzero_result;

#if defined(CONFIG_TC_NET_PBUF) || defined(PBUF_RED_PRIVATE_ABI)
	struct pbuf_test_args legacy_arg = { PBUF_RAW, 16, PBUF_RAM };
	nonzero_arg = (unsigned long)&legacy_arg;
#endif

	g_task11_pbuf_alloc_calls = 0;
	zero_result = test_net_pbuf(TESTIOC_NET_PBUF, 0);
	zero_allocs = g_task11_pbuf_alloc_calls;
	g_task11_pbuf_alloc_calls = 0;
	nonzero_result = test_net_pbuf(TESTIOC_NET_PBUF, nonzero_arg);
	nonzero_allocs = g_task11_pbuf_alloc_calls;
	printf("zero=%d zero_allocs=%u nonzero=%d nonzero_allocs=%u\n",
		zero_result, zero_allocs, nonzero_result, nonzero_allocs);
	return 0;
}
