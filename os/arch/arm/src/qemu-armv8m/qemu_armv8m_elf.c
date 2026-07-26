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

#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>

#include <tinyara/elf.h>
#include <arch/elf.h>

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline uint32_t qemu_armv8m_symbol_value(FAR const Elf32_Sym *sym)
{
	uint32_t value = sym != NULL ? sym->st_value : 0;

	if (sym != NULL && ELF32_ST_TYPE(sym->st_info) == STT_FUNC) {
		value |= 1;
	}

	return value;
}

static inline uint32_t qemu_armv8m_thumb32_opcode(uint32_t value)
{
	return (value << 16) | (value >> 16);
}

static inline uint16_t qemu_armv8m_thumb32_mov_imm(uint32_t opcode)
{
	return ((opcode >> 4) & 0xf000) |
	       ((opcode >> 15) & 0x0800) |
	       ((opcode >> 4) & 0x0700) |
	       (opcode & 0x00ff);
}

static inline uint32_t qemu_armv8m_thumb32_set_mov_imm(uint32_t opcode,
						       uint16_t imm)
{
	opcode &= ~((0xf << 16) | (1 << 26) | (0x7 << 12) | 0xff);
	opcode |= ((imm & 0xf000) << 4) |
		  ((imm & 0x0800) << 15) |
		  ((imm & 0x0700) << 4) |
		  (imm & 0x00ff);

	return opcode;
}

static inline uint32_t qemu_armv8m_get_thumb32(FAR const uint16_t *target)
{
	return ((uint32_t)target[1] << 16) | target[0];
}

static inline void qemu_armv8m_put_thumb32(FAR uint16_t *target,
					   uint32_t value)
{
	target[0] = (uint16_t)value;
	target[1] = (uint16_t)(value >> 16);
}

static void qemu_armv8m_relocate_thumb32_mov(uintptr_t addr,
					     uint32_t value,
					     bool upper)
{
	FAR uint16_t *target = (FAR uint16_t *)addr;
	uint32_t opcode = qemu_armv8m_thumb32_opcode(
		qemu_armv8m_get_thumb32(target));
	uint32_t addend = qemu_armv8m_thumb32_mov_imm(opcode);
	uint16_t imm;

	if (upper) {
		addend <<= 16;
		imm = (uint16_t)((value + addend) >> 16);
	} else {
		imm = (uint16_t)(value + addend);
	}

	opcode = qemu_armv8m_thumb32_set_mov_imm(opcode, imm);
	qemu_armv8m_put_thumb32(target, qemu_armv8m_thumb32_opcode(opcode));
}

static int qemu_armv8m_relocate_thumb32_branch(FAR const Elf32_Sym *sym,
					       uintptr_t addr,
					       uint32_t value)
{
	FAR uint16_t *upper = (FAR uint16_t *)addr;
	FAR uint16_t *lower = (FAR uint16_t *)(addr + 2);
	uint32_t upper_insn = *upper;
	uint32_t lower_insn = *lower;
	uint32_t encoded;
	uint32_t s;
	uint32_t j1;
	uint32_t j2;
	int32_t offset;

	s = (upper_insn >> 10) & 1;
	j1 = (lower_insn >> 13) & 1;
	j2 = (lower_insn >> 11) & 1;

	encoded = (s << 24) |
		  ((~(j1 ^ s) & 1) << 23) |
		  ((~(j2 ^ s) & 1) << 22) |
		  ((upper_insn & 0x03ff) << 12) |
		  ((lower_insn & 0x07ff) << 1);

	if (encoded & 0x01000000) {
		encoded -= 0x02000000;
	}

	offset = (int32_t)encoded + (int32_t)(value - addr);
	if (sym != NULL && ELF32_ST_TYPE(sym->st_info) == STT_FUNC &&
		(offset & 1) == 0) {
		return -EINVAL;
	}

	if (offset < -0x01000000 || offset >= 0x01000000) {
		return -EINVAL;
	}

	encoded = (uint32_t)offset;
	s = (encoded >> 24) & 1;
	j1 = s ^ (~(encoded >> 23) & 1);
	j2 = s ^ (~(encoded >> 22) & 1);

	upper_insn = (upper_insn & 0xf800) | (s << 10) |
		     ((encoded >> 12) & 0x03ff);
	lower_insn = (lower_insn & 0xd000) | (j1 << 13) | (j2 << 11) |
		     ((encoded >> 1) & 0x07ff);

	*upper = (uint16_t)upper_insn;
	*lower = (uint16_t)lower_insn;
	return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

bool up_checkarch(FAR const Elf32_Ehdr *hdr)
{
	static const unsigned char expected[EI_MAGIC_SIZE] = EI_MAGIC;

	return hdr->e_ident[EI_MAG0] == expected[EI_MAG0] &&
	       hdr->e_ident[EI_MAG1] == expected[EI_MAG1] &&
	       hdr->e_ident[EI_MAG2] == expected[EI_MAG2] &&
	       hdr->e_ident[EI_MAG3] == expected[EI_MAG3] &&
	       hdr->e_ident[EI_CLASS] == ELFCLASS32 &&
	       hdr->e_ident[EI_DATA] == ELFDATA2LSB &&
	       hdr->e_machine == EM_ARM;
}

int up_relocate(FAR const Elf32_Rel *rel, FAR const Elf32_Sym *sym,
		uintptr_t addr)
{
	uint32_t *target = (uint32_t *)addr;
	uint32_t addend;
	uint32_t value = qemu_armv8m_symbol_value(sym);

	switch (ELF32_R_TYPE(rel->r_info)) {
	case R_ARM_NONE:
		return OK;

	case R_ARM_ABS32:
	case R_ARM_TARGET1:
		*target += value;
		return OK;

	case R_ARM_REL32:
		*target += value - addr;
		return OK;

	case R_ARM_PREL31:
		addend = *target;
		*target = (*target & 0x80000000) |
			  ((addend + value - addr) & 0x7fffffff);
		return OK;

	case R_ARM_RELATIVE:
		return OK;

	case R_ARM_THM_CALL:
	case R_ARM_THM_JUMP24:
		return qemu_armv8m_relocate_thumb32_branch(sym, addr, value);

	case R_ARM_THM_MOVW_ABS_NC:
		qemu_armv8m_relocate_thumb32_mov(addr, value, false);
		return OK;

	case R_ARM_THM_MOVT_ABS:
		qemu_armv8m_relocate_thumb32_mov(addr, value, true);
		return OK;

	default:
		return -ENOSYS;
	}
}

int up_relocateadd(FAR const Elf32_Rela *rel, FAR const Elf32_Sym *sym,
		   uintptr_t addr)
{
	return -ENOSYS;
}
