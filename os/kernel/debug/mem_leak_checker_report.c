/****************************************************************************
 *
 * Copyright 2026 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ****************************************************************************/

#include <errno.h>
#include <limits.h>

#include "mem_leak_checker_report.h"

const char *mlc_report_reason_name(enum mlc_incomplete_reason_e reason)
{
	switch (reason) {
	case MLC_INCOMPLETE_NONE:
		return "NONE";
	case MLC_INCOMPLETE_ROOTS:
		return "ROOTS";
	case MLC_INCOMPLETE_CAPACITY:
		return "CAPACITY";
	case MLC_INCOMPLETE_CONTENTION:
		return "CONTENTION";
	case MLC_INCOMPLETE_TIMEOUT:
		return "DEADLINE";
	case MLC_INCOMPLETE_TASK_CONTEXT:
		return "TASK_CONTEXT";
	case MLC_INCOMPLETE_UNSUPPORTED_CONTEXT:
		return "UNSUPPORTED_CONTEXT";
	case MLC_INCOMPLETE_DOMAIN_CHANGED:
		return "DOMAIN_CHANGED";
	case MLC_INCOMPLETE_HEAP_CORRUPT:
		return "HEAP_CORRUPT";
	case MLC_INCOMPLETE_BUDGET:
		return "BUDGET";
	case MLC_FATAL_RESUME_AMBIGUOUS:
		return "FATAL_RESUME_AMBIGUOUS";
	case MLC_INCOMPLETE_PREOWNED_RESOURCE:
		return "PREOWNED_RESOURCE";
	case MLC_INCOMPLETE_GENERATION_EXHAUSTED:
		return "GENERATION_EXHAUSTED";
	case MLC_INCOMPLETE_CLOCK:
		return "CLOCK";
	case MLC_INCOMPLETE_DEADLINE:
		return "DEADLINE";
	case MLC_INCOMPLETE_BUSY_REGISTRY:
		return "BUSY_REGISTRY";
	case MLC_INCOMPLETE_BUSY_CRITICAL:
		return "BUSY_CRITICAL";
	case MLC_INCOMPLETE_BUSY_HEAP:
		return "BUSY_HEAP";
	case MLC_INCOMPLETE_INTERNAL:
		return "INTERNAL";
	default:
		return "INTERNAL";
	}
}

unsigned int mlc_report_reason_priority(enum mlc_incomplete_reason_e reason)
{
	switch (reason) {
	case MLC_INCOMPLETE_HEAP_CORRUPT:
		return 140u;
	case MLC_INCOMPLETE_DOMAIN_CHANGED:
		return 130u;
	case MLC_INCOMPLETE_TASK_CONTEXT:
		return 120u;
	case MLC_INCOMPLETE_ROOTS:
		return 115u;
	case MLC_INCOMPLETE_UNSUPPORTED_CONTEXT:
		return 100u;
	case MLC_INCOMPLETE_TIMEOUT:
		return 50u;
	case MLC_INCOMPLETE_CAPACITY:
		return 70u;
	case MLC_INCOMPLETE_BUDGET:
		return 60u;
	case MLC_INCOMPLETE_CONTENTION:
		return 30u;
	case MLC_INCOMPLETE_PREOWNED_RESOURCE:
		return 110u;
	case MLC_INCOMPLETE_GENERATION_EXHAUSTED:
		return 90u;
	case MLC_INCOMPLETE_CLOCK:
		return 80u;
	case MLC_INCOMPLETE_DEADLINE:
		return 50u;
	case MLC_INCOMPLETE_BUSY_REGISTRY:
		return 40u;
	case MLC_INCOMPLETE_BUSY_CRITICAL:
		return 30u;
	case MLC_INCOMPLETE_BUSY_HEAP:
		return 20u;
	case MLC_INCOMPLETE_INTERNAL:
		return 10u;
	case MLC_FATAL_RESUME_AMBIGUOUS:
		return UINT_MAX;
	case MLC_INCOMPLETE_NONE:
	default:
		return 0u;
	}
}

enum mlc_incomplete_reason_e mlc_report_select_reason(
		enum mlc_incomplete_reason_e current,
		enum mlc_incomplete_reason_e candidate)
{
	return mlc_report_reason_priority(candidate) >
		mlc_report_reason_priority(current) ? candidate : current;
}

enum mlc_report_primary_e mlc_report_primary(
		const struct mlc_report_summary_s *summary)
{
	if (summary == NULL || !summary->complete || summary->reason != MLC_INCOMPLETE_NONE ||
		summary->broken_count != 0) {
		return MLC_REPORT_PRIMARY_INCOMPLETE;
	}
	if (summary->definite_count != 0) {
		return MLC_REPORT_PRIMARY_DEFINITE;
	}
	if (summary->ambiguous_count != 0) {
		return MLC_REPORT_PRIMARY_AMBIGUOUS_ONLY;
	}
	return MLC_REPORT_PRIMARY_CLEAN;
}

const char *mlc_report_primary_name(enum mlc_report_primary_e primary)
{
	switch (primary) {
	case MLC_REPORT_PRIMARY_CLEAN:
		return "CLEAN";
	case MLC_REPORT_PRIMARY_DEFINITE:
		return "DEFINITE";
	case MLC_REPORT_PRIMARY_AMBIGUOUS_ONLY:
		return "AMBIGUOUS_ONLY";
	case MLC_REPORT_PRIMARY_INCOMPLETE:
	default:
		return "INCOMPLETE";
	}
}

int mlc_report_record_validate(const struct mlc_report_record_s *record)
{
	size_t dump_limit;

	if (record == NULL || record->address == 0) {
		return -EINVAL;
	}
	if (record->requested_size > record->capacity) {
		return -ERANGE;
	}
	dump_limit = mlc_report_dump_limit(record->requested_size);
	if (record->dump_size > dump_limit ||
		(record->dump_size != 0 && record->dump == NULL)) {
		return -ERANGE;
	}
	if (record->type == MLC_REPORT_RECORD_BROKEN &&
		(record->capacity != 0 || record->requested_size != 0 ||
			record->dump_size != 0)) {
		return -EINVAL;
	}
	if (record->type < MLC_REPORT_RECORD_DEFINITE ||
		record->type > MLC_REPORT_RECORD_BROKEN) {
		return -EINVAL;
	}
	return 0;
}

size_t mlc_report_dump_limit(size_t requested_size)
{
	return requested_size < MLC_REPORT_DUMP_MAX_BYTES ? requested_size :
		MLC_REPORT_DUMP_MAX_BYTES;
}

bool mlc_report_is_legacy_record(enum mlc_report_record_type_e type)
{
	return type == MLC_REPORT_RECORD_DEFINITE;
}
