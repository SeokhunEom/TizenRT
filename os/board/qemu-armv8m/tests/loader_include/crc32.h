#ifndef __TASK8_LOADER_CRC32_H
#define __TASK8_LOADER_CRC32_H

#include <stddef.h>
#include <stdint.h>

uint32_t crc32part(const uint8_t *src, size_t len, uint32_t crc32val);

#endif
