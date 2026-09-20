#!/usr/bin/env python3
"""Extract pinned C functions unchanged and exercise them with host I/O/transport stubs.

This is a review reproduction, not the repository UTC or board validation.
Usage: python3 reproduce.py [path-to-the-three-pinned-checkouts]
The generated host_repro.c is standalone and can be rerun without the checkouts.
"""
from pathlib import Path
import json
import subprocess
import sys
import tempfile

here = Path(__file__).resolve().parent
root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path((here / "source-root.txt").read_text().strip())
manifest = json.loads((here / "manifest.json").read_text())
for item in manifest:
    actual = subprocess.check_output(["git", "-C", str(root / item["repo"]), "rev-parse", "HEAD"], text=True).strip()
    assert actual == item["head"], (item["repo"], actual)

def excerpt(repo, path, first, last):
    lines = (root / repo / path).read_text().splitlines()
    return f'\n/* Unmodified {repo}/{path}:{first}-{last}; SHA in manifest.json. */\n' + "\n".join(lines[first - 1:last]) + "\n"

prefix = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>
#define DAWIT_SUCCESS 0
#define DAWIT_FAIL -1
#define DAWIT_INVALID_ARGS -2
#define DAWIT_TRUE 1
#define DAWIT_LOG_ERROR(...) ((void)0)
#define DAWIT_LOG_DEBUG(...) ((void)0)
#define DAWIT_LOG_INFO(...) ((void)0)
#define DAWIT_LOG_WARNING(...) ((void)0)
#define LOG_BUFFER_CODE_SIZE 7
#define SSM_MAX_ES_LOG_READ_LINES 20
#define SSM_MAX_SYS_LOG_READ_LINES 50
#define KPI_BUFFER_SIZE 50
#define SSM_STORED_LOGS_FILE "fixture.txt"
#define OK 0
#define ERROR -1
typedef int dawit_result;
typedef FILE ssm_file;
typedef struct log_data {
    int time;
    char code[LOG_BUFFER_CODE_SIZE];
    char *message;
    int level;
    struct log_data *pre, *next;
} log_data;
typedef struct kpi_file_data {
    char data_type[8];
    char *kpi_name, *kpi_message;
} kpi_file_data;
static kpi_file_data kpi_file_table[KPI_BUFFER_SIZE];
static unsigned int kpi_file_count;
static int es_seen, sys_seen, transport_result;
static char last_message[256], last_kpi[256];
static void SSM_free_sys_kpi_table(void);
static int SSM_is_policy_done(void) { return DAWIT_TRUE; }
static int SSM_Send_ES_logs_from_file(void *p) {
    for (log_data *node = p; node; node = node->next) {
        es_seen++;
        snprintf(last_message, sizeof(last_message), "%s", node->message ? node->message : "");
    }
    return transport_result;
}
static int SSM_Send_SYS_logs_from_file(void *p, unsigned int count) {
    kpi_file_data *table = p;
    sys_seen += count;
    if (count) snprintf(last_kpi, sizeof(last_kpi), "%s", table[0].kpi_name);
    return transport_result;
}
'''
helper = "DA-Service/SSM/src/SSM_file_helper.c"
logger = "DA-Service/SSM/src/SSM_file_logger.c"
parts = [prefix]
for first, last in [(14, 31), (34, 50), (53, 79), (103, 118)]:
    parts.append(excerpt("SSM_Release", helper, first, last))
parts.append(excerpt("SSM_Release", logger, 423, 819))
parts.append(excerpt("SSM_Release", logger, 831, 948))
parts.append(excerpt("TR_Utils", "apps/examples/crashrpt_comprehensive_test/crashrpt_comprehensive_test_main.c", 281, 347))
parts.append(r'''
static void fixture(const char *path, const char *text) {
    FILE *fp = fopen(path, "w"); assert(fp);
    assert(fputs(text, fp) >= 0); assert(fclose(fp) == 0);
}
static int run_case(const char *name, const char *input, int transport) {
    es_seen = sys_seen = 0; last_message[0] = last_kpi[0] = 0;
    transport_result = transport;
    fixture(SSM_STORED_LOGS_FILE, input);
    int result = SSM_read_logs_from_file_and_send_impl();
    printf("CASE %s: result=%d es=%d sys=%d first_sys=%s last_message=%s\n",
           name, result, es_seen, sys_seen, last_kpi, last_message);
    return result;
}
int main(void) {
    int result;
    result = run_case("mixed_two_SYS", "<0#1609459200#CS001#hello>\n<1#first#NUM#10>\n<1#second#NUM#20>\n", DAWIT_SUCCESS);
    assert(result == DAWIT_SUCCESS && es_seen == 1 && sys_seen == 1 && strcmp(last_kpi, "second") == 0);
    result = run_case("single_SYS", "<1#only#NUM#10>\n", DAWIT_SUCCESS);
    assert(result == DAWIT_FAIL && sys_seen == 0);
    result = run_case("transport_failure", "<0#1609459200#CS001#hello>\n<1#first#NUM#10>\n<1#second#NUM#20>\n", DAWIT_FAIL);
    assert(result == DAWIT_SUCCESS && es_seen == 1 && sys_seen == 1);
    run_case("hash_message", "<0#1609459200#CS001#before#after>\n", DAWIT_SUCCESS);
    assert(strcmp(last_message, "before") == 0);
    run_case("angle_message", "<0#1609459200#CS001#before>after>\n", DAWIT_SUCCESS);
    assert(strcmp(last_message, "before") == 0);
    fixture("wrong_reason.txt", "CRASH REPORT\nTest Case: 1\nExpected Reason: 2\nActual Reason: 3\nReport Sequence: 0\n");
    result = verify_report("wrong_reason.txt");
    printf("CASE wrong_reason: verify_result=%d (OK=%d)\n", result, OK);
    assert(result == OK);
    fixture("header_only.txt", "CRASH REPORT\n");
    result = verify_report("header_only.txt");
    printf("CASE header_only: verify_result=%d (OK=%d)\n", result, OK);
    assert(result == OK);
    puts("REPRODUCED: all seven observed defects/scenarios; these assertions characterize current bugs, not desired behavior.");
    return 0;
}
''')
source = here / "host_repro.c"
source.write_text("".join(parts))
with tempfile.TemporaryDirectory(prefix="ssm-host-repro-") as temp:
    binary = Path(temp) / "host_repro"
    build = subprocess.run(["clang", "-std=c11", "-g", "-fsanitize=address,undefined", str(source), "-o", str(binary)], text=True, capture_output=True)
    (here / "host-repro-build.txt").write_text(build.stdout + build.stderr)
    build.check_returncode()
    run = subprocess.run([str(binary)], cwd=temp, text=True, capture_output=True)
    (here / "host-repro-results.txt").write_text(run.stdout + run.stderr + f"\nexit_code={run.returncode}\n")
    print(run.stdout, end="")
    print(run.stderr, end="", file=sys.stderr)
    run.check_returncode()
