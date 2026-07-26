#include <stdio.h>
#include <string.h>

#include <tinyara/binary_manager.h>

#if defined(CONFIG_XIP_ELF)
#define EXPECTED_USER_HEADER_SIZE 48u
#define EXPECTED_COMMON_HEADER_SIZE 16u
#define ABI_VARIANT "xip"
#else
#define EXPECTED_USER_HEADER_SIZE 45u
#define EXPECTED_COMMON_HEADER_SIZE 14u
#define ABI_VARIANT "non-xip"
#endif

#if defined(CONFIG_BINARY_MANAGER)
#define MANAGER_VARIANT "bm"
#else
#define MANAGER_VARIANT "no-bm"
#endif

_Static_assert(BIN_NAME_MAX == 16, "binary name ABI changed");
_Static_assert(sizeof(user_binary_header_t) == EXPECTED_USER_HEADER_SIZE,
		"user binary header ABI changed");
_Static_assert(sizeof(kernel_binary_header_t) == 16u,
		"kernel binary header ABI changed");
_Static_assert(sizeof(common_binary_header_t) == EXPECTED_COMMON_HEADER_SIZE,
		"common binary header ABI changed");
_Static_assert(sizeof(resource_binary_header_t) == 14u,
		"resource binary header ABI changed");

int main(int argc, char **argv)
{
	user_binary_header_t header;

#if defined(CONFIG_BINARY_MANAGER)
	binary_update_info_t update;
	load_attr_t attr;

	memset(&update, 0, sizeof(update));
	memset(&attr, 0, sizeof(attr));
	(void)update;
	(void)attr;
#endif
	memset(&header, 0, sizeof(header));

	if (argc == 2 && strcmp(argv[1], "--malformed-name") == 0) {
		memset(header.bin_name, 'x', sizeof(header.bin_name));
		if (memchr(header.bin_name, '\0', sizeof(header.bin_name)) == NULL) {
			fprintf(stderr, "MALFORMED_NAME_REJECTED\n");
			return 2;
		}
		return 1;
	}

	printf("ABI %s %s user=%zu kernel=%zu common=%zu resource=%zu name=%d\n",
			MANAGER_VARIANT, ABI_VARIANT, sizeof(user_binary_header_t),
			sizeof(kernel_binary_header_t), sizeof(common_binary_header_t),
			sizeof(resource_binary_header_t), BIN_NAME_MAX);
	return 0;
}
