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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>

#include <stdint.h>

#include <tinyara/init.h>

#include "up_arch.h"
#include "up_internal.h"
#include "up_mpuinit.h"

#include "qemu_armv8m_internal.h"

#ifdef CONFIG_ARCH_FPU
#include "nvic.h"
#endif

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_ARCH_FPU
static inline void qemu_armv8m_fpuconfig(void);
#endif

/****************************************************************************
 * Public Data
 ****************************************************************************/

const uintptr_t g_idle_topstack = (uintptr_t)&_sidle_stack + CONFIG_IDLETHREAD_STACKSIZE;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_ARCH_FPU
static inline void qemu_armv8m_fpuconfig(void)
{
	uint32_t regval;

	regval = getcontrol();
#ifdef CONFIG_ARM_CMNVECTOR
	regval |= (1 << 2);
#else
	regval &= ~(1 << 2);
#endif
	setcontrol(regval);

	regval = getreg32(NVIC_FPCCR);
	regval &= ~((1 << 31) | (1 << 30));
	putreg32(regval, NVIC_FPCCR);

	regval = getreg32(NVIC_CPACR);
	regval |= ((3 << (2 * 10)) | (3 << (2 * 11)));
	putreg32(regval, NVIC_CPACR);
}
#else
#define qemu_armv8m_fpuconfig()
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void __start(void)
{
	const uint32_t *src;
	uint32_t *dest;

	for (dest = &_sbss; dest < &_ebss;) {
		*dest++ = 0;
	}

	for (src = &_eronly, dest = &_sdata; dest < &_edata;) {
		*dest++ = *src++;
	}

	qemu_armv8m_fpuconfig();
	qemu_armv8m_lowsetup();

#ifdef USE_EARLYSERIALINIT
	up_earlyserialinit();
#endif

	up_mpuinitialize();

#ifdef CONFIG_STACK_COLORATION
	go_os_start((FAR void *)&_sidle_stack, CONFIG_IDLETHREAD_STACKSIZE);
#else
	os_start();
#endif

	for (;;) {
	}
}
