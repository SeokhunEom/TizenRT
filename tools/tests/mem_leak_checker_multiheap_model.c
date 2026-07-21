#include <assert.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

enum {
	TEST_KMM_NHEAPS = 2,
	TEST_APP_REPORTS = 2,
	TEST_FIXED_EXCLUSION_RANGES = 30,
	TEST_FIXED_EXCLUSION_CAPACITY = 32
};

_Static_assert(TEST_FIXED_EXCLUSION_CAPACITY >= TEST_FIXED_EXCLUSION_RANGES,
	"fixed exclusion capacity must cover collector controls");

struct test_heap_s {
	const char *name;
	size_t leak_count;
};

struct test_report_s {
	const char *name;
	size_t leak_count;
};

struct test_pin_s {
	const struct test_heap_s *heap;
};

struct test_exclusion_s {
	uintptr_t begin;
	size_t size;
};

static size_t capture_kernel_reports(const struct test_heap_s *heaps,
		struct test_report_s *reports)
{
	size_t heap_index;

	for (heap_index = 0; heap_index < TEST_KMM_NHEAPS; heap_index++) {
		reports[heap_index].name = heaps[heap_index].name;
		reports[heap_index].leak_count = heaps[heap_index].leak_count;
	}
	return TEST_KMM_NHEAPS;
}

static size_t capture_app_reports(const struct test_pin_s *pins,
		size_t pin_count, struct test_report_s *reports, size_t report_index)
{
	size_t pin_index;

	for (pin_index = 0; pin_index < pin_count; pin_index++) {
		if (pins[pin_index].heap != NULL) {
			reports[report_index++] = (struct test_report_s){
				"app", pins[pin_index].heap->leak_count};
		}
	}
	return report_index;
}

static bool range_contains(const struct test_exclusion_s *range,
		uintptr_t address)
{
	return address >= range->begin && address - range->begin < range->size;
}

static size_t collect_candidates(const uintptr_t *allocations,
		size_t allocation_count, const struct test_exclusion_s *ranges,
		size_t range_count, uintptr_t *candidates)
{
	size_t allocation_index;
	size_t candidate_count = 0;

	for (allocation_index = 0; allocation_index < allocation_count;
		allocation_index++) {
		size_t range_index;
		bool excluded = false;

		for (range_index = 0; range_index < range_count; range_index++) {
			if (range_contains(&ranges[range_index],
					allocations[allocation_index])) {
				excluded = true;
				break;
			}
		}
		if (!excluded) {
			candidates[candidate_count++] = allocations[allocation_index];
		}
	}
	return candidate_count;
}

int main(void)
{
	static const struct test_heap_s heaps[TEST_KMM_NHEAPS] = {
		{"kernel", 1},
		{"kernel", 2}
	};
	static const struct test_pin_s pins[] = {
		{NULL},
		{&heaps[0]},
		{&heaps[1]}
	};
	static unsigned char report_rows[32];
	static unsigned char report_heaps[16];
	static unsigned char live_allocation[8];
	static const uintptr_t allocations[] = {
		(uintptr_t)report_rows,
		(uintptr_t)report_heaps,
		(uintptr_t)live_allocation
	};
	static const struct test_exclusion_s ranges[] = {
		{(uintptr_t)report_rows, sizeof(report_rows)},
		{(uintptr_t)report_heaps, sizeof(report_heaps)}
	};
	uintptr_t candidates[sizeof(allocations) / sizeof(allocations[0])];
	struct test_report_s reports[TEST_KMM_NHEAPS + TEST_APP_REPORTS] = {0};
	size_t report_count;
	size_t candidate_count;

	report_count = capture_kernel_reports(heaps, reports);
	assert(report_count == TEST_KMM_NHEAPS);
	assert(reports[0].leak_count == 1);
	assert(reports[1].leak_count == 2);
	report_count = capture_app_reports(pins, sizeof(pins) / sizeof(pins[0]),
		reports, report_count);
	assert(report_count == TEST_KMM_NHEAPS + TEST_APP_REPORTS);
	assert(reports[TEST_KMM_NHEAPS].leak_count == 1);
	assert(reports[TEST_KMM_NHEAPS + 1].leak_count == 2);
	candidate_count = collect_candidates(allocations,
		sizeof(allocations) / sizeof(allocations[0]), ranges,
		sizeof(ranges) / sizeof(ranges[0]), candidates);
	assert(candidate_count == 1 && candidates[0] == (uintptr_t)live_allocation);
	puts("MLC_HOST fixture=mlc_multi_kmm_heap_reports status=PASS");
	puts("MLC_HOST fixture=mlc_common_pin_report_offset status=PASS");
	puts("MLC_HOST fixture=mlc_report_control_exclusions status=PASS");
	return 0;
}
