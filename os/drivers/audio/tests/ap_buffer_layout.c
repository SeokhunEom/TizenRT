/* Copyright 2026 Samsung Electronics All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 * Compile against a configured audio board for all four test macro pairs.
 */
#include <tinyara/config.h>
#include <stddef.h>

#if APB_TEST_LARGE_BUFFERS
#define CONFIG_AUDIO_LARGE_BUFFERS 1
#else
#undef CONFIG_AUDIO_LARGE_BUFFERS
#endif
#if APB_TEST_MULTI_SESSION
#define CONFIG_AUDIO_MULTI_SESSION 1
#else
#undef CONFIG_AUDIO_MULTI_SESSION
#endif

#include <tinyara/audio/audio.h>

_Static_assert(_Alignof(struct ap_buffer_s) >= _Alignof(sem_t),
               "Audio buffers must align their semaphore");
_Static_assert(offsetof(struct ap_buffer_s, sem) % _Alignof(sem_t) == 0,
               "Audio semaphore must not be packed at an unaligned offset");
_Static_assert(_Alignof(struct ap_buffer_s) >= _Alignof(struct dq_entry_s),
               "Audio queue links must retain native alignment");
_Static_assert(offsetof(struct ap_buffer_s, dq_entry) == 0,
               "Audio queues rely on the link being the first member");
