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

#ifndef __OS_KERNEL_DEBUG_MEM_LEAK_CHECKER_REPORT_H
#define __OS_KERNEL_DEBUG_MEM_LEAK_CHECKER_REPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mem_leak_checker_lifecycle.h"

#define MLC_REPORT_DUMP_MAX_BYTES 32u

enum mlc_report_record_type_e {
	MLC_REPORT_RECORD_DEFINITE = 0,
	MLC_REPORT_RECORD_AMBIGUOUS,
	MLC_REPORT_RECORD_DETAIL,
	MLC_REPORT_RECORD_BROKEN
};

enum mlc_report_primary_e {
	MLC_REPORT_PRIMARY_CLEAN = 0,
	MLC_REPORT_PRIMARY_DEFINITE,
	MLC_REPORT_PRIMARY_AMBIGUOUS_ONLY,
	MLC_REPORT_PRIMARY_INCOMPLETE
};

struct mlc_report_record_s {
	enum mlc_report_record_type_e type;
	uintptr_t address;
	size_t capacity;
	size_t requested_size;
	uintptr_t owner;
	int pid;
	const unsigned char *dump;
	size_t dump_size;
	size_t scc_id;
	uint8_t provenance;
};

struct mlc_report_summary_s {
	size_t definite_count;
	size_t ambiguous_count;
	size_t broken_count;
	bool complete;
	enum mlc_incomplete_reason_e reason;
};

const char *mlc_report_reason_name(enum mlc_incomplete_reason_e reason);

unsigned int mlc_report_reason_priority(enum mlc_incomplete_reason_e reason);

enum mlc_incomplete_reason_e mlc_report_select_reason(
		enum mlc_incomplete_reason_e current,
		enum mlc_incomplete_reason_e candidate);

enum mlc_report_primary_e mlc_report_primary(
		const struct mlc_report_summary_s *summary);

const char *mlc_report_primary_name(enum mlc_report_primary_e primary);

int mlc_report_record_validate(const struct mlc_report_record_s *record);

size_t mlc_report_dump_limit(size_t requested_size);

bool mlc_report_is_legacy_record(enum mlc_report_record_type_e type);

#endif
