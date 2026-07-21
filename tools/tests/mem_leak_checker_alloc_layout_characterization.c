#include <stddef.h>
#include <stdint.h>

typedef int16_t mlc_pid_t;

struct mlc_allocnode32_before_s { uint32_t preceding; uint32_t alloc_call_addr; mlc_pid_t pid; uint16_t memory_state; uint32_t size; };
struct mlc_allocnode32_after_s { uint32_t preceding; uint32_t alloc_call_addr; mlc_pid_t pid; uint16_t alloc_padding; uint32_t size; };
struct mlc_allocnode64_before_s { size_t preceding; void *alloc_call_addr; mlc_pid_t pid; uint16_t memory_state; size_t size; };
struct mlc_allocnode64_after_s { size_t preceding; void *alloc_call_addr; mlc_pid_t pid; uint16_t alloc_padding; size_t size; };

#define ASSERT_RENAME_ONLY(before_type, after_type, old_field, new_field) \
	_Static_assert(sizeof(before_type) == sizeof(after_type), "allocation header size changed"); \
	_Static_assert(offsetof(before_type, size) == offsetof(after_type, size), "size offset changed"); \
	_Static_assert(offsetof(before_type, old_field) == offsetof(after_type, new_field), "debug field offset changed")

ASSERT_RENAME_ONLY(struct mlc_allocnode32_before_s, struct mlc_allocnode32_after_s, memory_state, alloc_padding);
ASSERT_RENAME_ONLY(struct mlc_allocnode64_before_s, struct mlc_allocnode64_after_s, memory_state, alloc_padding);
_Static_assert(sizeof(mlc_pid_t) == 2, "repository pid_t model must be int16_t");
_Static_assert(sizeof(struct mlc_allocnode32_before_s) == 16, "unexpected 32-bit model layout");
_Static_assert(offsetof(struct mlc_allocnode32_before_s, size) == 12, "unexpected 32-bit size offset");
_Static_assert(sizeof(size_t) == 8, "64-bit host characterization requires 64-bit size_t");
_Static_assert(sizeof(void *) == 8, "64-bit host characterization requires 64-bit pointers");
_Static_assert(sizeof(struct mlc_allocnode64_before_s) == 32, "unexpected 64-bit model layout");
_Static_assert(offsetof(struct mlc_allocnode64_before_s, size) == 24, "unexpected 64-bit size offset");

int main(void) { return 0; }
