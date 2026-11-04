/****************************************************************************
 *
 * Copyright 2016 Samsung Electronics All Rights Reserved.
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

#ifndef __EXAMPLES_TESTCASE_TESTCASE_COMMON_H
#define __EXAMPLES_TESTCASE_TESTCASE_COMMON_H
#include <tinyara/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

enum tc_op_type_e {
	TC_START,
	TC_END,
	TC_OP_TYPE_MAX
};
typedef enum tc_op_type_e tc_op_type_t;

extern int total_pass;
extern int total_fail;

/* Keep the failing test and source location even on small-flash targets. */
#ifdef CONFIG_TESTCASE_COMPACT_RESULT
#define TC_RECORD_FAILURE(api, format, ...) \
	tc_record_failure("\n[%s] FAIL [Line : %d] %s\n", __func__, __LINE__, api)
#else
#define TC_RECORD_FAILURE(api, format, ...) \
	tc_record_failure(format, __func__, __LINE__, api, __VA_ARGS__)
#endif

#define TC_ASSERT_CLEANUP(api_name, var, freeResource) \
{\
	if (!(var)) {\
		TC_RECORD_FAILURE(api_name, "\n[%s] FAIL [Line : %d] %s : Following expression is not true: %s\n", #var); \
		freeResource; \
		return; \
	} \
}

#define TC_ASSERT(api_name, var) TC_ASSERT_CLEANUP(api_name, var, ((void)0))

#define TC_ASSERT_EQ_CLEANUP(api_name, var, ref, freeResource) \
{\
	if ((var) != (ref)) {\
		TC_RECORD_FAILURE(api_name, "\n[%s] FAIL [Line : %d] %s : Values (%s == 0x%x) and (%s == 0x%x) are not equal\n", #var, (int)(var), #ref, (int)(ref)); \
		freeResource; \
		return; \
	} \
}

#define TC_ASSERT_EQ_ERROR_CLEANUP(api_name, var, ref, error, freeResource) \
{\
	if ((var) != (ref)) {\
		TC_RECORD_FAILURE(api_name, "\n[%s] FAIL [Line : %d] %s : API returned = %d, Values (%s == 0x%x) and (%s == 0x%x) are not equal\n", error, #var, (int)(var), #ref, (int)(ref)); \
		freeResource; \
		return; \
	} \
}

#define TC_ASSERT_EQ_RETURN(api_name, var, ref, ret) \
{ \
	if ((var) != (ref)) { \
		TC_RECORD_FAILURE(api_name, "\n[%s] FAIL [Line : %d] %s : Values (%s == 0x%x) and (%s == 0x%x) are not equal\n", #var, (int)(var), #ref, (int)(ref)); \
		return ret; \
	} \
}

#define TC_ASSERT_EQ(api_name, var, ref) TC_ASSERT_EQ_RETURN(api_name, var, ref, ((void)0))

#define TC_ASSERT_NEQ_CLEANUP(api_name, var, ref, freeResource) \
{\
	if ((var) == (ref)) {\
		TC_RECORD_FAILURE(api_name, "\n[%s] FAIL [Line : %d] %s : Values (%s == 0x%x) and (%s == 0x%x) are equal\n", #var, (int)(var), #ref, (int)(ref)); \
		freeResource; \
		return; \
	} \
}

#define TC_ASSERT_NEQ(api_name, var, ref) TC_ASSERT_NEQ_CLEANUP(api_name, var, ref, ((void)0))

#define TC_ASSERT_GT_CLEANUP(api_name, var, ref, freeResource) \
{\
	if ((var) <= (ref)) {\
		TC_RECORD_FAILURE(api_name, "\n[%s] FAIL [Line : %d] %s : Values (%s == 0x%x) is not greater than (%s == 0x%x)\n", #var, (int)(var), #ref, (int)(ref)); \
		freeResource; \
		return; \
	} \
}

#define TC_ASSERT_GT(api_name, var, ref) TC_ASSERT_GT_CLEANUP(api_name, var, ref, ((void)0))

#define TC_ASSERT_GEQ_CLEANUP(api_name, var, ref, freeResource) \
{\
	if ((var) < (ref)) {\
		TC_RECORD_FAILURE(api_name, "\n[%s] FAIL [Line : %d] %s : Values (%s == 0x%x) is not greater than or equal to (%s == 0x%x)\n", #var, (int)(var), #ref, (int)(ref)); \
		freeResource; \
		return; \
	} \
}

#define TC_ASSERT_GEQ(api_name, var, ref) TC_ASSERT_GEQ_CLEANUP(api_name, var, ref, ((void)0))

#define TC_ASSERT_LT_CLEANUP(api_name, var, ref, freeResource) \
{\
	if ((var) >= (ref)) {\
		TC_RECORD_FAILURE(api_name, "\n[%s] FAIL [Line : %d] %s : Values (%s == 0x%x) is not lower than (%s == 0x%x)\n", #var, (int)(var), #ref, (int)(ref)); \
		freeResource; \
		return; \
	} \
}

#define TC_ASSERT_LT(api_name, var, ref) TC_ASSERT_LT_CLEANUP(api_name, var, ref, ((void)0))

#define TC_ASSERT_LEQ_CLEANUP(api_name, var, ref, freeResource) \
{\
	if ((var) > (ref)) {\
		TC_RECORD_FAILURE(api_name, "\n[%s] FAIL [Line : %d] %s : Values (%s == 0x%x) is not lower than or equal to (%s == 0x%x)\n", #var, (int)(var), #ref, (int)(ref)); \
		freeResource; \
		return; \
	} \
}

#define TC_ASSERT_LEQ(api_name, var, ref) TC_ASSERT_LEQ_CLEANUP(api_name, var, ref, ((void)0))

#define TC_SUCCESS_RESULT() \
{\
	tc_record_success(__func__); \
}

#define TC_FREE_MEMORY(buffer) \
{\
	if ((buffer) != NULL) { \
		free(buffer); \
		buffer = NULL; \
	} \
}

#ifdef __cplusplus
extern "C" {
#endif

/* Shared reporters keep assertion diagnostics out of each test body. */
void tc_record_failure(const char *format, ...);
void tc_record_success(const char *function);
int testcase_state_handler(tc_op_type_t type, const char *tc_name);

#ifdef __cplusplus
}
#endif
#endif
