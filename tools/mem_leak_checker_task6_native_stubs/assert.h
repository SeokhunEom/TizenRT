#ifndef __MLC_TASK6_NATIVE_ASSERT_H
#define __MLC_TASK6_NATIVE_ASSERT_H

#include_next <assert.h>

#define ASSERT(condition) assert(condition)
#define ASSERT_INFO(condition, format, ...) \
	do { \
		(void)(format); \
		(void)(0, __VA_ARGS__); \
		assert(condition); \
	} while (0)

#endif
