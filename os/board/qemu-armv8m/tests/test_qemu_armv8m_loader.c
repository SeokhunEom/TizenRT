#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include <tinyara/binary_manager.h>
#include <tinyara/binfmt/binfmt.h>

#define TEST_SLOT_BASE 0x102c0000UL
#define TEST_SLOT_END  0x10400000UL
#define TEST_COMMON_CAPACITY 0x000a0000UL

enum write_mode_e {
	WRITE_NORMAL,
	WRITE_ZERO_THEN_ERROR,
};

static enum write_mode_e g_write_mode;
static int g_write_calls;
static bool g_close_fails;
static bool g_common_file;
static bool g_app_file;
static int g_unlink_calls;
static int g_load_calls;
static int g_exec_calls;
static bool g_alloc_fails;
static int g_load_result;
static int g_exec_result;
static struct binary_s g_bins[4];
static size_t g_bin_count;
static uint32_t g_common_data[8];
static uint32_t *g_umm_app_id;
struct binary_s *g_lib_binp;
static char g_log[4096];
static size_t g_log_length;

int loader_test_open(const char *path, int flags, ...);
ssize_t loader_test_write(int fd, const void *buffer, size_t size);
int loader_test_close(int fd);
int loader_test_unlink(const char *path);

#define open loader_test_open
#define write loader_test_write
#define close loader_test_close
#define unlink loader_test_unlink
#include "../src/qemu_armv8m_boot.c"
#undef open
#undef write
#undef close
#undef unlink

#define TEST_CHECK(condition, message) \
	do { \
		if (!(condition)) { \
			fprintf(stderr, "FAIL %s: %s\nlog=%s\n", __func__, message, g_log); \
			return false; \
		} \
	} while (0)

void loader_test_log(const char *format, ...)
{
	va_list arguments;
	int result;

	if (g_log_length >= sizeof(g_log)) {
		return;
	}

	va_start(arguments, format);
	result = vsnprintf(g_log + g_log_length, sizeof(g_log) - g_log_length,
					 format, arguments);
	va_end(arguments);
	if (result > 0) {
		size_t appended = (size_t)result;
		size_t available = sizeof(g_log) - g_log_length;
		g_log_length += appended < available ? appended : available - 1;
	}
}

static bool *test_file_for_path(const char *path)
{
	if (strcmp(path, QEMU_COMMON_PATH) == 0) {
		return &g_common_file;
	}
	if (strcmp(path, QEMU_APP1_PATH) == 0) {
		return &g_app_file;
	}
	return NULL;
}

int loader_test_open(const char *path, int flags, ...)
{
	bool *file = test_file_for_path(path);
	(void)flags;
	if (!file) {
		return -ENOENT;
	}
	*file = true;
	return file == &g_common_file ? 10 : 11;
}

ssize_t loader_test_write(int fd, const void *buffer, size_t size)
{
	(void)fd;
	(void)buffer;
	g_write_calls++;
	if (g_write_mode == WRITE_ZERO_THEN_ERROR) {
		return g_write_calls == 1 ? 0 : -EIO;
	}
	return (ssize_t)size;
}

int loader_test_close(int fd)
{
	(void)fd;
	return g_close_fails ? -EIO : 0;
}

int loader_test_unlink(const char *path)
{
	bool *file = test_file_for_path(path);
	if (!file) {
		return -ENOENT;
	}
	*file = false;
	g_unlink_calls++;
	return 0;
}

uint32_t crc32part(const uint8_t *src, size_t len, uint32_t crc32val)
{
	uint32_t crc = ~crc32val;
	size_t index;
	unsigned int bit;

	for (index = 0; index < len; index++) {
		crc ^= src[index];
		for (bit = 0; bit < 8; bit++) {
			crc = (crc >> 1) ^ (0xedb88320UL & (uint32_t)-(int32_t)(crc & 1));
		}
	}
	return ~crc;
}

void *kmm_zalloc(size_t size)
{
	struct binary_s *bin;
	if (g_alloc_fails || size != sizeof(struct binary_s) || g_bin_count >= 4) {
		return NULL;
	}
	bin = &g_bins[g_bin_count++];
	memset(bin, 0, sizeof(*bin));
	return bin;
}

void kmm_free(void *memory)
{
	(void)memory;
}

void exec_getsymtab(const struct symtab_s **symtab, int *nsymbols)
{
	*symtab = NULL;
	*nsymbols = 0;
}

int load_module(struct binary_s *bin)
{
	g_load_calls++;
	bin->sections[BIN_DATA] = TEST_SLOT_BASE + 0x1000;
	bin->sections[BIN_HEAP] = TEST_SLOT_BASE + 0x1100;
	return g_load_result;
}

int unload_module(struct binary_s *bin)
{
	(void)bin;
	return 0;
}

int exec_module(struct binary_s *bin)
{
	(void)bin;
	g_exec_calls++;
	return g_exec_result;
}

void elf_save_bin_section_addr(struct binary_s *bin)
{
	(void)bin;
}

void binfmt_arch_init_mem_protect(struct binary_s *bin)
{
	(void)bin;
}

static void map_slots(void)
{
	void *mapped = mmap((void *)TEST_SLOT_BASE, TEST_SLOT_END - TEST_SLOT_BASE,
						PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED,
						-1, 0);
	if (mapped != (void *)TEST_SLOT_BASE) {
		perror("mmap QEMU package slots");
		exit(2);
	}
}

static void reset_fixture(void)
{
	memset((void *)TEST_SLOT_BASE, 0, TEST_SLOT_END - TEST_SLOT_BASE);
	g_write_mode = WRITE_NORMAL;
	g_write_calls = 0;
	g_close_fails = false;
	g_common_file = false;
	g_app_file = false;
	g_unlink_calls = 0;
	g_load_calls = 0;
	g_exec_calls = 0;
	g_alloc_fails = false;
	g_load_result = 0;
	g_exec_result = 42;
	g_bin_count = 0;
	memset(g_bins, 0, sizeof(g_bins));
	memset(g_common_data, 0, sizeof(g_common_data));
	g_umm_app_id = NULL;
	g_lib_binp = NULL;
	g_log_length = 0;
	g_log[0] = '\0';
}

static void finish_package(uint8_t *package, size_t package_size)
{
	uint32_t checksum = crc32part(package + QEMU_BINARY_CHECKSUM_SIZE,
							  package_size - QEMU_BINARY_CHECKSUM_SIZE, 0);
	memcpy(package, &checksum, sizeof(checksum));
}

static void make_common(size_t payload_size, bool corrupt_crc)
{
	common_binary_header_t *header = (common_binary_header_t *)QEMU_COMMON_LOADADDR;
	size_t package_size;

	memset(header, 0, sizeof(*header));
	header->header_size = sizeof(*header) - QEMU_BINARY_CHECKSUM_SIZE;
	header->version = 1;
	header->bin_size = (uint32_t)payload_size;
	package_size = QEMU_BINARY_CHECKSUM_SIZE + header->header_size + payload_size;
	if (package_size <= TEST_COMMON_CAPACITY) {
		memset((uint8_t *)header + sizeof(*header), 0x43, payload_size);
		finish_package((uint8_t *)header, package_size);
		if (corrupt_crc) {
			((uint8_t *)header)[package_size - 1] ^= 0x01;
		}
	}
}

static void make_app(size_t payload_size, bool corrupt_crc)
{
	user_binary_header_t *header = (user_binary_header_t *)QEMU_APP1_LOADADDR;
	size_t package_size;

	memset(header, 0, sizeof(*header));
	header->header_size = sizeof(*header) - QEMU_BINARY_CHECKSUM_SIZE;
	header->bin_priority = 100;
	header->bin_size = (uint32_t)payload_size;
	memcpy(header->bin_name, CONFIG_APP1_BIN_NAME, sizeof(CONFIG_APP1_BIN_NAME));
	header->bin_ver = 1;
	header->bin_ramsize = 4096;
	header->bin_stacksize = 2048;
	package_size = QEMU_BINARY_CHECKSUM_SIZE + header->header_size + payload_size;
	if (package_size <= TEST_COMMON_CAPACITY) {
		memset((uint8_t *)header + sizeof(*header), 0x41, payload_size);
		finish_package((uint8_t *)header, package_size);
		if (corrupt_crc) {
			((uint8_t *)header)[package_size - 1] ^= 0x01;
		}
	}
}

static bool baseline_valid_app1_starts(void)
{
	reset_fixture();
	make_app(32, false);
	TEST_CHECK(qemu_armv8m_load_app1() == OK, "valid app1 was rejected");
	TEST_CHECK(g_exec_calls == 1, "valid app1 did not execute exactly once");
	TEST_CHECK(strstr(g_log, "QEMU_APP1_STARTED pid=42") != NULL,
			   "valid app1 start marker missing");
	TEST_CHECK(strstr(g_log, "QEMU_LOAD_REJECT") == NULL,
			   "valid app1 emitted rejection marker");
	return true;
}

static bool oversized_common_is_rejected(void)
{
	reset_fixture();
	make_common(TEST_COMMON_CAPACITY, false);
	TEST_CHECK(qemu_armv8m_load_common() < 0, "oversized common was accepted");
	TEST_CHECK(g_load_calls == 0, "oversized common reached load_module");
	TEST_CHECK(strstr(g_log, "QEMU_LOAD_REJECT common size") != NULL,
			   "oversized common reason was unstable");
	return true;
}

static bool oversized_app1_is_rejected(void)
{
	reset_fixture();
	make_app(TEST_COMMON_CAPACITY, false);
	TEST_CHECK(qemu_armv8m_load_app1() < 0, "oversized app1 was accepted");
	TEST_CHECK(g_exec_calls == 0, "oversized app1 executed");
	TEST_CHECK(strstr(g_log, "QEMU_LOAD_REJECT app1 size") != NULL,
			   "oversized app1 reason was unstable");
	return true;
}

static bool corrupt_common_crc_is_rejected(void)
{
	reset_fixture();
	make_common(32, true);
	TEST_CHECK(qemu_armv8m_load_common() < 0, "corrupt common CRC was accepted");
	TEST_CHECK(g_load_calls == 0, "corrupt common reached load_module");
	TEST_CHECK(strstr(g_log, "QEMU_LOAD_REJECT common crc") != NULL,
			   "corrupt common reason was unstable");
	return true;
}

static bool corrupt_app1_crc_is_rejected(void)
{
	reset_fixture();
	make_app(32, true);
	TEST_CHECK(qemu_armv8m_load_app1() < 0, "corrupt app1 CRC was accepted");
	TEST_CHECK(g_exec_calls == 0, "corrupt app1 executed");
	TEST_CHECK(strstr(g_log, "QEMU_LOAD_REJECT app1 crc") != NULL,
			   "corrupt app1 reason was unstable");
	return true;
}

static bool zero_write_fails_without_retry(void)
{
	reset_fixture();
	make_app(32, false);
	g_write_mode = WRITE_ZERO_THEN_ERROR;
	TEST_CHECK(qemu_armv8m_load_app1() < 0, "zero write was accepted");
	TEST_CHECK(g_write_calls == 1, "zero write was retried and can loop forever");
	TEST_CHECK(!g_app_file, "partial app1 file survived zero write");
	TEST_CHECK(g_unlink_calls == 1, "zero write did not unlink exactly once");
	TEST_CHECK(strstr(g_log, "QEMU_LOAD_REJECT app1 write") != NULL,
			   "zero-write reason was unstable");
	return true;
}

static bool close_failure_removes_file(void)
{
	reset_fixture();
	make_app(32, false);
	g_close_fails = true;
	TEST_CHECK(qemu_armv8m_load_app1() < 0, "close failure was accepted");
	TEST_CHECK(!g_app_file, "app1 file survived close failure");
	TEST_CHECK(g_exec_calls == 0, "app1 executed after close failure");
	TEST_CHECK(g_unlink_calls == 1, "close failure did not unlink exactly once");
	TEST_CHECK(strstr(g_log, "QEMU_LOAD_REJECT app1 close") != NULL,
			   "close-failure reason was unstable");
	return true;
}

static bool common_failure_blocks_app1(void)
{
	common_binary_header_t *header;
	reset_fixture();
	header = (common_binary_header_t *)QEMU_COMMON_LOADADDR;
	header->header_size = 0;
	make_app(32, false);
	board_initialize();
	TEST_CHECK(g_exec_calls == 0, "app1 started after common rejection");
	TEST_CHECK(strstr(g_log, "QEMU_LOAD_REJECT common header") != NULL,
			   "common rejection marker missing");
	TEST_CHECK(strstr(g_log, "QEMU_APP1_STARTED") == NULL,
			   "forbidden app1-start marker was emitted");
	return true;
}

static bool allocation_failure_removes_file(void)
{
	reset_fixture();
	make_app(32, false);
	g_alloc_fails = true;
	TEST_CHECK(qemu_armv8m_load_app1() < 0, "allocation failure was accepted");
	TEST_CHECK(!g_app_file, "app1 file survived allocation failure");
	TEST_CHECK(g_unlink_calls == 1, "allocation failure did not unlink exactly once");
	TEST_CHECK(strstr(g_log, "QEMU_LOAD_REJECT app1 alloc") != NULL,
			   "allocation-failure reason was unstable");
	return true;
}

static bool load_failure_removes_file(void)
{
	reset_fixture();
	make_app(32, false);
	g_load_result = -ENOEXEC;
	TEST_CHECK(qemu_armv8m_load_app1() < 0, "load failure was accepted");
	TEST_CHECK(!g_app_file, "app1 file survived load failure");
	TEST_CHECK(g_unlink_calls == 1, "load failure did not unlink exactly once");
	TEST_CHECK(strstr(g_log, "QEMU_LOAD_REJECT app1 load") != NULL,
			   "load-failure reason was unstable");
	return true;
}

static bool exec_failure_removes_file(void)
{
	reset_fixture();
	make_app(32, false);
	g_exec_result = -ENOEXEC;
	TEST_CHECK(qemu_armv8m_load_app1() < 0, "exec failure was accepted");
	TEST_CHECK(!g_app_file, "app1 file survived exec failure");
	TEST_CHECK(g_unlink_calls == 1, "exec failure did not unlink exactly once");
	TEST_CHECK(strstr(g_log, "QEMU_LOAD_REJECT app1 exec") != NULL,
			   "exec-failure reason was unstable");
	return true;
}

static bool valid_common_then_app1_starts(void)
{
	reset_fixture();
	make_common(32, false);
	make_app(32, false);
	board_initialize();
	TEST_CHECK(g_load_calls == 2, "valid common/app1 sequence did not load both");
	TEST_CHECK(g_exec_calls == 1, "valid app1 did not execute exactly once");
	TEST_CHECK(strstr(g_log, "QEMU_APP1_STARTED") != NULL,
			   "stable app1-start marker missing");
	TEST_CHECK(strstr(g_log, "QEMU_LOAD_REJECT") == NULL,
			   "valid sequence emitted rejection marker");
	return true;
}

struct test_case_s {
	const char *name;
	bool (*run)(void);
};

static const struct test_case_s g_cases[] = {
	{"baseline", baseline_valid_app1_starts},
	{"oversize-common", oversized_common_is_rejected},
	{"oversize-app1", oversized_app1_is_rejected},
	{"corrupt-common", corrupt_common_crc_is_rejected},
	{"corrupt-app1", corrupt_app1_crc_is_rejected},
	{"zero-write", zero_write_fails_without_retry},
	{"close-error", close_failure_removes_file},
	{"common-before-app1", common_failure_blocks_app1},
	{"alloc-error", allocation_failure_removes_file},
	{"load-error", load_failure_removes_file},
	{"exec-error", exec_failure_removes_file},
	{"valid-sequence", valid_common_then_app1_starts},
};

int main(int argc, char **argv)
{
	size_t index;
	int failures = 0;

	map_slots();
	for (index = 0; index < sizeof(g_cases) / sizeof(g_cases[0]); index++) {
		if (argc > 1 && strcmp(argv[1], "all") != 0 &&
			strcmp(argv[1], g_cases[index].name) != 0) {
			continue;
		}
		if (!g_cases[index].run()) {
			failures++;
		} else {
			printf("PASS %s\n", g_cases[index].name);
			if (argc > 1 && strcmp(argv[1], "all") != 0) {
				printf("TRACE %s", g_log);
			}
		}
	}
	if (argc > 1 && strcmp(argv[1], "all") != 0) {
		for (index = 0; index < sizeof(g_cases) / sizeof(g_cases[0]); index++) {
			if (strcmp(argv[1], g_cases[index].name) == 0) {
				return failures == 0 ? 0 : 1;
			}
		}
		fprintf(stderr, "unknown case: %s\n", argv[1]);
		return 2;
	}
	printf("loader cases: %zu, failures: %d\n",
		   sizeof(g_cases) / sizeof(g_cases[0]), failures);
	return failures == 0 ? 0 : 1;
}
