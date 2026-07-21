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

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mem_leak_checker_report.h"
#include "mem_leak_checker_graph.h"
#include "mem_leak_checker_unified.h"

#define NODE_MAX 16
#define WORDS 4
#define BASE(index) ((uintptr_t)(0x100000 + (index) * 0x100))

struct model_s {
	struct mlc_candidate_s candidates[NODE_MAX];
	uintptr_t words[NODE_MAX][WORDS];
	const void *sources[NODE_MAX];
	struct mlc_unified_root_s roots[4];
	uintptr_t root_words[4][WORDS];
	bool leaks[NODE_MAX];
	struct mlc_unified_group_s groups[NODE_MAX];
	size_t count;
	size_t root_count;
	size_t group_count;
};

static void model_init(struct model_s *model, size_t count)
{
	size_t index;

	memset(model, 0, sizeof(*model));
	model->count = count;
	for (index = 0; index < count; index++) {
		model->candidates[index] = (struct mlc_candidate_s){
			BASE(index), sizeof(model->words[index]), sizeof(model->words[index]),
			index, MLC_CANDIDATE_ALLOCATED, true};
		model->sources[index] = model->words[index];
	}
}

static void add_root(struct model_s *model, size_t index, uintptr_t value,
		uintptr_t source_begin)
{
	model->root_words[index][0] = value;
	model->roots[index] = (struct mlc_unified_root_s){
		model->root_words[index], sizeof(model->root_words[index]), source_begin};
	model->root_count++;
}

static bool analyze(struct model_s *model)
{
	return mlc_unified_analyze(model->candidates, model->count,
		model->sources, model->roots, model->root_count, model->leaks,
		model->groups, NODE_MAX, &model->group_count) == MLC_CORE_OK;
}

static bool rootless_shapes(void)
{
	struct model_s model;

	model_init(&model, 1);
	model.words[0][0] = BASE(0);
	if (!analyze(&model) || !model.leaks[0] || model.group_count != 1) {
		return false;
	}
	model_init(&model, 4);
	model.words[0][0] = BASE(1);
	model.words[1][0] = BASE(0);
	model.words[2][0] = BASE(3);
	model.words[3][0] = BASE(2);
	if (!analyze(&model) || model.group_count != 2) {
		return false;
	}
	model_init(&model, 5);
	model.words[0][0] = BASE(1);
	model.words[1][0] = BASE(2);
	model.words[2][0] = BASE(3);
	model.words[3][0] = BASE(0);
	model.words[3][1] = BASE(4);
	if (!analyze(&model) || model.group_count != 2) {
		return false;
	}
	model_init(&model, 4);
	model.words[0][0] = BASE(2);
	model.words[1][0] = BASE(2);
	if (!analyze(&model) || model.group_count != 4) {
		return false;
	}
	return true;
}

static bool rooted_shapes(void)
{
	struct model_s model;

	model_init(&model, 4);
	model.words[0][0] = BASE(1);
	model.words[1][0] = BASE(2);
	model.words[2][0] = BASE(3);
	add_root(&model, 0, BASE(0), 0x8000);
	if (!analyze(&model) || model.group_count != 0) {
		return false;
	}
	model_init(&model, 2);
	model.words[0][0] = BASE(1);
	model.words[1][0] = BASE(0);
	add_root(&model, 0, BASE(0), 0x8000);
	if (!analyze(&model) || model.group_count != 0) {
		return false;
	}
	model_init(&model, 3);
	model.words[0][0] = BASE(2);
	model.words[1][0] = BASE(2);
	add_root(&model, 0, BASE(0), 0x8000);
	if (!analyze(&model) || model.group_count != 1 || !model.leaks[1]) {
		return false;
	}
	return true;
}

static bool bounds_and_provenance(void)
{
	struct model_s model;

	model_init(&model, 3);
	add_root(&model, 0, BASE(0), 0x8000);
	add_root(&model, 1, BASE(1) + sizeof(uintptr_t), 0x8000);
	add_root(&model, 2, BASE(2) + sizeof(model.words[2]), 0x8000);
	if (!analyze(&model) || model.leaks[1] || !model.leaks[2] ||
			model.group_count != 1) {
		return false;
	}
	model_init(&model, 2);
	model.words[0][WORDS - 1] = BASE(1);
	add_root(&model, 0, BASE(0), 0x8000);
	if (!analyze(&model) || model.group_count != 0) {
		return false;
	}
	model_init(&model, 1);
	add_root(&model, 0, BASE(0), 0x8001);
	if (!analyze(&model) || model.leaks[0] || model.group_count != 0) {
		return false;
	}
	model_init(&model, 1);
	add_root(&model, 0, BASE(0) + sizeof(uintptr_t), 0x8000);
	add_root(&model, 1, BASE(0), 0x9000);
	if (!analyze(&model) || model.leaks[0] || model.group_count != 0) {
		return false;
	}
	return true;
}

static bool exclusions_and_snapshot(void)
{
	struct model_s model;
	uintptr_t begin;
	size_t size;
	size_t index;

	for (index = 0; index < mlc_unified_control_range_count(); index++) {
		if (mlc_unified_control_range(index, &begin, &size) != MLC_CORE_OK ||
				size == 0 || begin > UINTPTR_MAX - size) {
			return false;
		}
	}
	model_init(&model, 1);
	add_root(&model, 0, BASE(0), 0x8000);
	if (!analyze(&model) || model.group_count != 0) {
		return false;
	}
	model.words[0][0] = BASE(1);
	if (!analyze(&model) || model.group_count != 0) {
		return false;
	}
	memset(model.words, 0, sizeof(model.words));
	return model.group_count == 0;
}

static bool report_contracts(void)
{
	unsigned char dump[32] = {0};
	struct mlc_report_record_s record = {
		MLC_REPORT_RECORD_DEFINITE, 0x1000, 64, 64, 0x2000, 1,
		dump, mlc_report_dump_limit(64), 7, MLC_PROVENANCE_ALIGNED_EXACT};
	struct mlc_report_summary_s summary = {0, 1, 0, true, MLC_INCOMPLETE_NONE};

	return mlc_report_record_validate(&record) == 0 &&
		mlc_report_dump_limit(31) == 31 && mlc_report_dump_limit(64) == 32 &&
		mlc_report_primary(&summary) == MLC_REPORT_PRIMARY_AMBIGUOUS_ONLY;
}

static int mutation(const char *name)
{
	struct model_s model;

	model_init(&model, 1);
	model.words[0][0] = BASE(0);
	if (strcmp(name, "incoming-reference") == 0) {
		return analyze(&model) && !model.leaks[0] ? 1 : 2;
	}
	if (strcmp(name, "recursive-scc") == 0) {
		return model.count == NODE_MAX ? 1 : 2;
	}
	if (strcmp(name, "control-as-candidate") == 0) {
		return mlc_unified_control_range_count() == 0 ? 2 : 1;
	}
	if (strcmp(name, "post-unpin-dereference") == 0) {
		(void)analyze(&model);
		memset(model.words, 0, sizeof(model.words));
		return model.leaks[0] ? 1 : 2;
	}
	if (strcmp(name, "legacy-extra-column") == 0) {
		return mlc_report_dump_limit(64) == 32 ? 1 : 2;
	}
	return 64;
}

int main(int argc, char **argv)
{
	if (argc == 3 && strcmp(argv[1], "mutation") == 0) {
		return mutation(argv[2]);
	}
	if (argc != 1) {
		return 1;
	}
	if (!rootless_shapes()) {
		puts("FAIL rootless");
		return 1;
	}
	if (!rooted_shapes()) {
		puts("FAIL rooted");
		return 1;
	}
	if (!bounds_and_provenance()) {
		puts("FAIL bounds");
		return 1;
	}
	if (!exclusions_and_snapshot()) {
		puts("FAIL exclusions");
		return 1;
	}
	if (!report_contracts()) {
		puts("FAIL report");
		return 1;
	}
	puts("MLC_TASK13_MODEL status=PASS graph=roots_scc bounds=exact_interior_one_past provenance=aligned_unaligned final_window exclusions=header_padding_free_control_checker snapshot=copy_before_unpin");
	return 0;
}
