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
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 ****************************************************************************/
#ifndef __KERNEL_HEALTH_MONITOR_ACCESS_H
#define __KERNEL_HEALTH_MONITOR_ACCESS_H
#include <stdint.h>

/* Kernel architecture protocol, not ISO C thread synchronization. Explicit
 * word instructions prevent tearing, merging and speculative compiler loads.
 * On SMP the amebasmart kernel RAM mapping must be coherent and shareable.
 * Full DMB orders loads as well as stores (SP_DMB is only dmb st).
 */
static inline uint32_t health_load(const uint32_t *address)
{
#ifdef CONFIG_SMP
	uint32_t value;
	__asm__ __volatile__("ldr %0, [%1]" : "=r"(value) : "r"(address) : "memory");
	return value;
#else
	return *(const volatile uint32_t *)address;
#endif
}

static inline void health_store(uint32_t *address, uint32_t value)
{
#ifdef CONFIG_SMP
	__asm__ __volatile__("str %1, [%0]" : : "r"(address), "r"(value) : "memory");
#else
	*(volatile uint32_t *)address = value;
#endif
}

static inline void health_barrier(void)
{
#ifdef CONFIG_SMP
	__asm__ __volatile__("dmb sy" : : : "memory");
#else
	__asm__ __volatile__("" : : : "memory");
#endif
}
#endif
