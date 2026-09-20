/* Temporary single-core QEMU verification controls. Not a product API. */
#ifndef HM_QEMU_PROBE_H
#define HM_QEMU_PROBE_H
#if defined(CONFIG_SMP) || !defined(CONFIG_BUILD_FLAT) || (!defined(CONFIG_ARCH_CHIP_LM3S6965) && !defined(CONFIG_ARCH_CHIP_MPS2_AN505))
#error "Health Monitor QEMU probes require the single-core LM3S or MPS2-AN505 flat test target"
#endif
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
void hm_qemu_control(bool pause, bool virtual_time, uint32_t now);
uint32_t hm_qemu_now(void);
bool hm_qemu_paused(void);
unsigned int hm_qemu_count(void);
void hm_qemu_hint_busy(bool busy);
void hm_qemu_fault(pid_t pid, uint32_t now, uint32_t deadline);
#endif
