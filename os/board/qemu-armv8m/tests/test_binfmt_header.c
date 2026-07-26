#include <stdio.h>
#include <tinyara/binfmt/binfmt.h>

int main(void)
{
	struct binary_s binary = { 0 };

#ifdef CONFIG_APP_BINARY_SEPARATION
	binary.binary_idx = 1;
	binary.bin_ver = 260718;
#ifdef CONFIG_OPTIMIZE_APP_RELOAD_TIME
	binary.bin_name[0] = 'a';
	printf("BINFMT no-bm idx=%u version=%lu name_capacity=%zu\n",
			(unsigned int)binary.binary_idx, (unsigned long)binary.bin_ver,
			sizeof(binary.bin_name));
#else
	binary.bin_name = "app1";
#endif
#else
	printf("BINFMT bm base_size=%zu\n", sizeof(binary));
#endif

	return 0;
}
