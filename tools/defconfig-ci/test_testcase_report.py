"""Exercise real testcase reporting and assertion cleanup on the host."""

from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
TESTCASE = ROOT / "apps/examples/testcase"


class TestcaseReportTests(unittest.TestCase):
    def test_assertion_results_cleanup_and_return_values(self):
        self.check_reporting(compact=False)

    def test_compact_assertions_keep_failures_cleanup_and_return_values(self):
        self.check_reporting(compact=True)

    def check_reporting(self, compact):
        source = r'''
#include "tc_common.h"
int total_pass;
int total_fail;
static int cleaned;
static void success(void) { TC_ASSERT_EQ("equal", 7, 7); TC_SUCCESS_RESULT(); }
static void cleanup(void) { TC_ASSERT_EQ_CLEANUP("cleanup", 1, 2, cleaned++); }
static int value(void) { TC_ASSERT_EQ_RETURN("return", 1, 2, 42); return 0; }
static void truth(void) { TC_ASSERT("truth", 0); }
static void unequal(void) { TC_ASSERT_NEQ("unequal", 1, 1); }
static void greater(void) { TC_ASSERT_GT("greater", 1, 2); }
static void greater_equal(void) { TC_ASSERT_GEQ("greater_equal", 1, 2); }
static void lower(void) { TC_ASSERT_LT("lower", 2, 1); }
static void lower_equal(void) { TC_ASSERT_LEQ("lower_equal", 2, 1); }
static void error(void) { TC_ASSERT_EQ_ERROR_CLEANUP("error", 1, 2, -7, cleaned++); }
int main(void)
{
    success(); cleanup(); truth(); unequal(); greater(); greater_equal();
    lower(); lower_equal(); error();
    if (value() != 42 || total_pass != 1 || total_fail != 9 || cleaned != 2) return 1;
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / "tinyara/config.h"
            config.parent.mkdir()
            config.write_text("#define CONFIG_TESTCASE_COMPACT_RESULT 1\n" if compact else "")
            fixture = Path(directory) / "report.c"
            binary = Path(directory) / "report"
            fixture.write_text(source)
            subprocess.run(
                ["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-I", str(TESTCASE), "-I", directory,
                 str(fixture), str(TESTCASE / "tc_common.c"), "-o", str(binary)],
                check=True, capture_output=True, text=True,
            )
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            self.assertEqual(result.stdout.count(" PASS "), 1)
            self.assertEqual(result.stdout.count(" FAIL "), 9)
            self.assertIn("[success] PASS", result.stdout)
            self.assertRegex(result.stdout, r"\[cleanup\] FAIL \[Line : \d+\] cleanup")
            if compact:
                self.assertNotIn("Values", result.stdout)
            else:
                self.assertIn("API returned = -7", result.stdout)
                self.assertIn("cleanup : Values (1 == 0x1) and (2 == 0x2)", result.stdout)


if __name__ == "__main__":
    unittest.main()
