/* SPDX-License-Identifier: Apache-2.0 */
#ifndef HEALTH_MONITOR_TEST_REBOOT_REASON_H
#define HEALTH_MONITOR_TEST_REBOOT_REASON_H
#include <stdbool.h>
#include <stdint.h>
#include <tinyara/reboot_reason.h>
void up_reboot_reason_write(reboot_reason_code_t reason);
bool up_reboot_reason_is_written(void);
void up_reboot_reason_write_by_addr(uintptr_t addr, reboot_reason_code_t reason);
#endif
