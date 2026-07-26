#include <tinyara/os_api_test_drv.h>

#if defined(CONFIG_TC_KERNEL_NET_PBUF) && !defined(CONFIG_TC_NET_PBUF) && defined(TASK11_LWIP_PBUF_INCLUDED)
#error "kernel-only public header included lwip/pbuf.h"
#endif

int main(void)
{
#ifdef CONFIG_TC_NET_PBUF
	return sizeof(struct pbuf_test_args) == 0;
#else
	return 0;
#endif
}
