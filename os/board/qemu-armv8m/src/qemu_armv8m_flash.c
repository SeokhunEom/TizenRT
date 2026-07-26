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

#include <tinyara/config.h>

#ifdef CONFIG_FLASH_PARTITION

#include <stdint.h>

#include <tinyara/fs/mtd.h>

FAR struct mtd_dev_s *up_flashinitialize(void)
{
	return rammtd_initialize((FAR uint8_t *)CONFIG_FLASH_START_ADDR,
			CONFIG_FLASH_SIZE);
}

#endif /* CONFIG_FLASH_PARTITION */

#ifdef CONFIG_BOARDCTL_RESET

#include "up_internal.h"

int board_reset(int status)
{
	(void)status;
	up_systemreset();
	return 0;
}

#endif /* CONFIG_BOARDCTL_RESET */

#ifdef CONFIG_LIB_BOARDCTL

int board_app_initialize(void)
{
	return 0;
}

#endif /* CONFIG_LIB_BOARDCTL */
