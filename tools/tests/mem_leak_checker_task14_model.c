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
#include <pthread.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mem_leak_checker_report.h"

#define MLC14_RESOURCE_COUNT 5u
#define MLC14_FAULT_COUNT 20u
#define MLC14_CONTRACT_CLASS_COUNT 20u
#define MLC14_CHURN_REPEAT 500u

enum mlc14_fault_e {
	MLC14_FAULT_NONE = 0,
	MLC14_FAULT_REGISTRY,
	MLC14_FAULT_CRITICAL,
	MLC14_FAULT_HEAP,
	MLC14_FAULT_PREOWNED_CRITICAL,
	MLC14_FAULT_PREOWNED_HEAP,
	MLC14_FAULT_DOMAIN,
	MLC14_FAULT_TASK,
	MLC14_FAULT_REGISTER,
	MLC14_FAULT_PREPAUSED,
	MLC14_FAULT_PAUSE,
	MLC14_FAULT_CANCEL,
	MLC14_FAULT_RESUME,
	MLC14_FAULT_STALE_IPI,
	MLC14_FAULT_GENERATION,
	MLC14_FAULT_CORRUPT,
	MLC14_FAULT_PADDING,
	MLC14_FAULT_CAPACITY,
	MLC14_FAULT_BUDGET,
	MLC14_FAULT_POST_UNPIN
};

static const char *const g_fault_names[MLC14_FAULT_COUNT] = {
	"none", "registry", "critical", "heap", "preowned-critical",
	"preowned-heap", "domain", "task", "register", "prepaused", "pause",
	"cancel", "resume", "stale-ipi", "generation", "corrupt", "padding",
	"capacity", "budget", "post-unpin"
};

static const char *const g_contract_classes[MLC14_CONTRACT_CLASS_COUNT] = {
	"admission", "registry_contention", "global_critical_contention",
	"heap_contention", "caller_preowned", "domain_drift",
	"task_context_drift", "register_drift", "prepaused",
	"pause_cancel_resume_race", "stale_reordered_ipi", "generation_exhaustion",
	"heap_corruption", "invalid_padding", "capacity", "operation_budget",
	"deadline", "post_unpin_poison", "reentrancy", "graph_churn"
};

struct mlc14_model_s {
	atomic_bool admitted;
	atomic_uint generation;
};

struct mlc14_invocation_s {
	struct mlc14_model_s *model;
	enum mlc14_fault_e fault;
	bool owned[MLC14_RESOURCE_COUNT];
	unsigned int released[MLC14_RESOURCE_COUNT];
	unsigned int generation;
	bool incomplete;
};

struct mlc14_budget_s {
	size_t remaining;
	uint64_t now;
	uint64_t deadline;
};

static bool mlc14_fault_at(enum mlc14_fault_e fault,
		enum mlc14_fault_e expected)
{
	return fault == expected;
}

static void mlc14_unwind(struct mlc14_invocation_s *invocation)
{
	size_t resource;

	for (resource = MLC14_RESOURCE_COUNT; resource > 0; resource--) {
		size_t index = resource - 1;

		if (invocation->owned[index]) {
			invocation->owned[index] = false;
			invocation->released[index]++;
		}
	}
}

static int mlc14_begin(struct mlc14_invocation_s *invocation,
		enum mlc14_fault_e fault)
{
	struct mlc14_model_s *model = invocation->model;

	memset(invocation->owned, 0, sizeof(invocation->owned));
	memset(invocation->released, 0, sizeof(invocation->released));
	invocation->fault = fault;
	invocation->incomplete = false;
	if (atomic_exchange_explicit(&model->admitted, true,
			memory_order_acq_rel)) {
		return -EBUSY;
	}
	invocation->generation = atomic_fetch_add_explicit(&model->generation, 1,
			memory_order_acq_rel) + 1;
	if (mlc14_fault_at(fault, MLC14_FAULT_REGISTRY) ||
		mlc14_fault_at(fault, MLC14_FAULT_CAPACITY) ||
		mlc14_fault_at(fault, MLC14_FAULT_BUDGET)) {
		goto incomplete;
	}
	if (fault == MLC14_FAULT_PREOWNED_CRITICAL ||
		fault == MLC14_FAULT_PREOWNED_HEAP) {
		goto incomplete;
	}
	invocation->owned[0] = true;
	if (mlc14_fault_at(fault, MLC14_FAULT_DOMAIN) ||
		mlc14_fault_at(fault, MLC14_FAULT_TASK) ||
		mlc14_fault_at(fault, MLC14_FAULT_REGISTER)) {
		goto incomplete;
	}
	invocation->owned[1] = true;
	if (mlc14_fault_at(fault, MLC14_FAULT_CRITICAL)) {
		goto incomplete;
	}
	invocation->owned[2] = true;
	if (mlc14_fault_at(fault, MLC14_FAULT_HEAP) ||
		mlc14_fault_at(fault, MLC14_FAULT_CORRUPT) ||
		mlc14_fault_at(fault, MLC14_FAULT_PADDING) ||
		mlc14_fault_at(fault, MLC14_FAULT_POST_UNPIN)) {
		goto incomplete;
	}
	if (fault == MLC14_FAULT_PREPAUSED) {
		goto incomplete;
	}
	invocation->owned[3] = true;
	if (mlc14_fault_at(fault, MLC14_FAULT_PAUSE) ||
		mlc14_fault_at(fault, MLC14_FAULT_CANCEL) ||
		mlc14_fault_at(fault, MLC14_FAULT_RESUME) ||
		mlc14_fault_at(fault, MLC14_FAULT_STALE_IPI) ||
		mlc14_fault_at(fault, MLC14_FAULT_GENERATION)) {
		goto incomplete;
	}
	invocation->owned[4] = true;
	return 0;

incomplete:
	invocation->incomplete = true;
	mlc14_unwind(invocation);
	atomic_store_explicit(&model->admitted, false, memory_order_release);
	return -EIO;
}

static int mlc14_finish(struct mlc14_invocation_s *invocation)
{
	if (invocation->incomplete) {
		return -EIO;
	}
	mlc14_unwind(invocation);
	atomic_store_explicit(&invocation->model->admitted, false,
		memory_order_release);
	return 0;
}

static bool mlc14_released_exactly_once(
		const struct mlc14_invocation_s *invocation)
{
	size_t index;

	for (index = 0; index < MLC14_RESOURCE_COUNT; index++) {
		if (invocation->owned[index] || invocation->released[index] > 1) {
			return false;
		}
	}
	return true;
}

static bool mlc14_run_fault_matrix(struct mlc14_model_s *model,
		bool *fault_ok)
{
	struct mlc14_invocation_s invocation;
	enum mlc14_fault_e fault;

	for (fault = MLC14_FAULT_REGISTRY; fault < MLC14_FAULT_COUNT; fault++) {
		if (g_fault_names[fault] == NULL || g_fault_names[fault][0] == '\0') {
			return false;
		}
		invocation.model = model;
		fault_ok[fault] = mlc14_begin(&invocation, fault) == -EIO &&
			invocation.incomplete && mlc14_released_exactly_once(&invocation);
		if (!fault_ok[fault]) {
			return false;
		}
		invocation.model = model;
		if (mlc14_begin(&invocation, MLC14_FAULT_NONE) != 0 ||
			mlc14_finish(&invocation) != 0 ||
			!mlc14_released_exactly_once(&invocation)) {
			return false;
		}
	}
	return !atomic_load_explicit(&model->admitted, memory_order_acquire);
}

static bool mlc14_check_contract_classes(const bool *fault_ok, bool admission,
		bool budget, bool deadline, bool reentrancy, bool graph_churn)
{
	bool class_ok[MLC14_CONTRACT_CLASS_COUNT] = {
		admission,
		fault_ok[MLC14_FAULT_REGISTRY],
		fault_ok[MLC14_FAULT_CRITICAL],
		fault_ok[MLC14_FAULT_HEAP],
		fault_ok[MLC14_FAULT_PREOWNED_CRITICAL] &&
			fault_ok[MLC14_FAULT_PREOWNED_HEAP],
		fault_ok[MLC14_FAULT_DOMAIN],
		fault_ok[MLC14_FAULT_TASK],
		fault_ok[MLC14_FAULT_REGISTER],
		fault_ok[MLC14_FAULT_PREPAUSED],
		fault_ok[MLC14_FAULT_PAUSE] && fault_ok[MLC14_FAULT_CANCEL] &&
			fault_ok[MLC14_FAULT_RESUME],
		fault_ok[MLC14_FAULT_STALE_IPI],
		fault_ok[MLC14_FAULT_GENERATION],
		fault_ok[MLC14_FAULT_CORRUPT],
		fault_ok[MLC14_FAULT_PADDING],
		fault_ok[MLC14_FAULT_CAPACITY],
		budget && fault_ok[MLC14_FAULT_BUDGET],
		deadline,
		fault_ok[MLC14_FAULT_POST_UNPIN],
		reentrancy,
		graph_churn
	};
	size_t index;
	size_t executed = 0;

	for (index = 0; index < MLC14_CONTRACT_CLASS_COUNT; index++) {
		if (g_contract_classes[index] == NULL ||
			g_contract_classes[index][0] == '\0' || !class_ok[index]) {
			return false;
		}
		executed++;
	}
	return executed == MLC14_CONTRACT_CLASS_COUNT;
}

struct mlc14_holder_s {
	struct mlc14_model_s *model;
	atomic_bool entered;
	atomic_bool release;
};

static void *mlc14_holder(void *argument)
{
	struct mlc14_holder_s *holder = argument;

	if (atomic_exchange_explicit(&holder->model->admitted, true,
			memory_order_acq_rel)) {
		return NULL;
	}
	atomic_store_explicit(&holder->entered, true, memory_order_release);
	while (!atomic_load_explicit(&holder->release, memory_order_acquire)) {
	}
	atomic_store_explicit(&holder->model->admitted, false, memory_order_release);
	return NULL;
}

static bool mlc14_busy_race(struct mlc14_model_s *model)
{
	struct mlc14_holder_s holder = { model, false, false };
	struct mlc14_invocation_s invocation = { .model = model };
	pthread_t thread;
	unsigned int spin;
	int result;

	if (pthread_create(&thread, NULL, mlc14_holder, &holder) != 0) {
		return false;
	}
	for (spin = 0; spin < 1000000u &&
			!atomic_load_explicit(&holder.entered, memory_order_acquire); spin++) {
	}
	result = mlc14_begin(&invocation, MLC14_FAULT_NONE);
	atomic_store_explicit(&holder.release, true, memory_order_release);
	if (pthread_join(thread, NULL) != 0) {
		return false;
	}
	return spin < 1000000u && result == -EBUSY &&
		!atomic_load_explicit(&model->admitted, memory_order_acquire);
}

static bool mlc14_reentrancy(struct mlc14_model_s *model)
{
	struct mlc14_invocation_s outer = { .model = model };
	struct mlc14_invocation_s inner = { .model = model };

	if (mlc14_begin(&outer, MLC14_FAULT_NONE) != 0 ||
		mlc14_begin(&inner, MLC14_FAULT_NONE) != -EBUSY ||
		mlc14_finish(&outer) != 0) {
		return false;
	}
	return mlc14_begin(&inner, MLC14_FAULT_NONE) == 0 &&
		mlc14_finish(&inner) == 0;
}

static bool mlc14_budget_boundaries(void)
{
	static const uint64_t boundaries[] = { 10, 20, 40, 80, 95 };
	struct mlc14_budget_s budget = { 8, 0, 95 };
	size_t index;

	if (budget.remaining < 8 || budget.now >= budget.deadline) {
		return false;
	}
	budget.remaining -= 8;
	budget.now = boundaries[0];
	if (budget.remaining != 0 || budget.now >= budget.deadline) {
		return false;
	}
	for (index = 1; index < sizeof(boundaries) / sizeof(boundaries[0]); index++) {
		budget.now = boundaries[index];
		if (index + 1 == sizeof(boundaries) / sizeof(boundaries[0])) {
			if (budget.now < budget.deadline) {
				return false;
			}
		} else if (budget.now >= budget.deadline) {
			return false;
		}
	}
	return true;
}

static bool mlc14_graph_churn(struct mlc14_model_s *model)
{
	struct mlc_report_summary_s summary;
	unsigned int iteration;

	for (iteration = 0; iteration < MLC14_CHURN_REPEAT; iteration++) {
		bool corrupt = iteration % 17u == 0;
		struct mlc14_invocation_s invocation = { .model = model };
		int result;

		result = mlc14_begin(&invocation,
			corrupt ? MLC14_FAULT_CORRUPT : MLC14_FAULT_NONE);
		if ((corrupt && result != -EIO) || (!corrupt && result != 0) ||
			(!corrupt && mlc14_finish(&invocation) != 0)) {
			return false;
		}
		summary.definite_count = 0;
		summary.ambiguous_count = corrupt ? 0 : 1;
		summary.broken_count = corrupt ? 1 : 0;
		summary.complete = !corrupt;
		summary.reason = corrupt ? MLC_INCOMPLETE_HEAP_CORRUPT :
			MLC_INCOMPLETE_NONE;
		if (corrupt && mlc_report_primary(&summary) !=
			MLC_REPORT_PRIMARY_INCOMPLETE) {
			return false;
		}
		if (!corrupt && mlc_report_primary(&summary) !=
			MLC_REPORT_PRIMARY_AMBIGUOUS_ONLY) {
			return false;
		}
	}
	return true;
}

static bool mlc14_run_once(void)
{
	struct mlc14_model_s model;
	bool fault_ok[MLC14_FAULT_COUNT] = { false };
	bool admission;
	bool budget;
	bool deadline;
	bool reentrancy;
	bool graph_churn;

	atomic_init(&model.admitted, false);
	atomic_init(&model.generation, 0);
	if (!mlc14_run_fault_matrix(&model, fault_ok)) {
		return false;
	}
	admission = mlc14_busy_race(&model);
	reentrancy = mlc14_reentrancy(&model);
	budget = mlc14_budget_boundaries();
	deadline = budget;
	graph_churn = mlc14_graph_churn(&model);
	return mlc14_check_contract_classes(fault_ok, admission, budget, deadline,
		reentrancy, graph_churn);
}

int main(int argc, char **argv)
{
	unsigned int repeat = 1;
	unsigned int iteration;

	if (argc > 2 || (argc == 2 && sscanf(argv[1], "%u", &repeat) != 1) ||
		repeat == 0) {
		return 64;
	}
	for (iteration = 0; iteration < repeat; iteration++) {
		if (!mlc14_run_once()) {
			puts("MLC_TASK14_MODEL status=FAIL");
			return 1;
		}
	}
	printf("MLC_TASK14_MODEL status=PASS repeat=%u faults=%u churn=%u boundaries=10,20,40,80,95 release=exact_once verdict=incomplete_suppressed\n",
		repeat, MLC14_CONTRACT_CLASS_COUNT, MLC14_CHURN_REPEAT);
	return 0;
}
