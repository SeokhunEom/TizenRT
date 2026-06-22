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

#ifndef __INCLUDE_TINYARA_UACCESS_H
#define __INCLUDE_TINYARA_UACCESS_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>
#include <tinyara/compiler.h>

#ifndef __ASSEMBLY__

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef __user
#define __user
#endif

#if defined(CONFIG_BUILD_FLAT) || defined(__KERNEL__)

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C" {
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Name: copy_from_user
 *
 * Description:
 *   Copy data from a user-space buffer to a kernel buffer.
 *
 * Return Value:
 *   The number of bytes that could not be copied.  Zero means success.  If
 *   the user-space range is rejected, the destination buffer is zero-filled.
 *
 ****************************************************************************/

EXTERN unsigned long copy_from_user(FAR void *to, FAR const void __user *from,
									unsigned long n);

/****************************************************************************
 * Name: copy_to_user
 *
 * Description:
 *   Copy data from a kernel buffer to a user-space buffer.
 *
 * Return Value:
 *   The number of bytes that could not be copied.  Zero means success.
 *
 ****************************************************************************/

EXTERN unsigned long copy_to_user(FAR void __user *to, FAR const void *from,
								  unsigned long n);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif							/* CONFIG_BUILD_FLAT || __KERNEL__ */
#endif							/* __ASSEMBLY__ */
#endif							/* __INCLUDE_TINYARA_UACCESS_H */
