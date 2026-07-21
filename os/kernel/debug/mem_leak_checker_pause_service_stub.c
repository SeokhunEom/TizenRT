#ifndef MLC_PAUSE_HOST_TEST
#include <tinyara/config.h>

#include <errno.h>
#include <stdint.h>

#include "mem_leak_checker_pause.h"

#if !defined(CONFIG_SMP) || CONFIG_SMP_NCPUS != 2
bool mlc_pause_poll_pending(int cpu, uintptr_t saved_flags)
{
	(void)cpu;
	(void)saved_flags;
	return false;
}

bool mlc_pause_service_poll(int cpu, uintptr_t saved_flags)
{
	(void)cpu;
	(void)saved_flags;
	return false;
}

bool mlc_pause_service_irq(int cpu, int irq, const void *context,
		uint32_t token)
{
	(void)cpu;
	(void)irq;
	(void)context;
	(void)token;
	return false;
}

uint32_t mlc_pause_service_token(int cpu)
{
	(void)cpu;
	return 0;
}

int mlc_pause_owner_request_cpu(int cpu, uint32_t token, uint64_t epoch_usec)
{
	(void)cpu;
	(void)token;
	(void)epoch_usec;
	return -ENOTSUP;
}

enum mlc_pause_state_e mlc_pause_owner_state_cpu(int cpu, uint32_t token)
{
	(void)cpu;
	(void)token;
	return MLC_PAUSE_FATAL;
}

int mlc_pause_owner_cancel_cpu(int cpu, uint32_t token)
{
	(void)cpu;
	(void)token;
	return -ENOTSUP;
}

int mlc_pause_owner_resume_cpu(int cpu, uint32_t token)
{
	(void)cpu;
	(void)token;
	return -ENOTSUP;
}

int mlc_pause_owner_recycle_cpu(int cpu, uint32_t token)
{
	(void)cpu;
	(void)token;
	return -ENOTSUP;
}

bool mlc_pause_owner_drained_cpu(int cpu, uint32_t token)
{
	(void)cpu;
	(void)token;
	return false;
}
#endif
#endif
