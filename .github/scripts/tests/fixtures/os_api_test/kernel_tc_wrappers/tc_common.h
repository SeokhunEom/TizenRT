#ifndef __TASK16_TC_COMMON_H
#define __TASK16_TC_COMMON_H

#include <stdio.h>

#define OK 0
#define ERROR -1

enum tc_op_type_e {
	TC_START,
	TC_END,
};

typedef enum tc_op_type_e tc_op_type_t;

extern int total_pass;
extern int total_fail;
int testcase_state_handler(tc_op_type_t type, const char *tc_name);

#define TC_ASSERT_EQ(api_name, var, ref) \
	if ((var) != (ref)) { \
		printf("[%s] FAIL %s\n", __func__, api_name); \
		total_fail++; \
		return; \
	}

#define TC_SUCCESS_RESULT() \
	do { \
		printf("[%s] PASS\n", __func__); \
		total_pass++; \
	} while (0)

#endif
