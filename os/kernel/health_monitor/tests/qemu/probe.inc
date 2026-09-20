/* Included only in an explicitly enabled temporary QEMU test firmware. */
static bool g_hm_qemu_pause;
static bool g_hm_qemu_virtual;
static uint32_t g_hm_qemu_now;

void hm_qemu_control(bool pause, bool virtual_time, uint32_t now)
{
	irqstate_t flags = irqsave();
	g_hm_qemu_pause = pause;
	g_hm_qemu_virtual = virtual_time;
	g_hm_qemu_now = now;
	irqrestore(flags);
}

uint32_t hm_qemu_now(void)
{
	return g_hm_qemu_virtual ? g_hm_qemu_now : (uint32_t)clock_systimer();
}

bool hm_qemu_paused(void)
{
	return g_hm_qemu_pause;
}

unsigned int hm_qemu_count(void)
{
	irqstate_t flags = irqsave();
	unsigned int count = g_health_count;
	irqrestore(flags);
	return count;
}

void hm_qemu_fault(pid_t pid, uint32_t now, uint32_t deadline)
{
	/* Only fatal-test diagnostics, after the registry lock is released. */
	lldbg("HM_QEMU verdict pid=%d now=%lu deadline=%lu\n",
		(int)pid, (unsigned long)now, (unsigned long)deadline);
}

void hm_qemu_hint_busy(bool busy)
{
	irqstate_t flags = irqsave();
	/* Deliberately incomplete publication: no registry writers run until
	 * this temporary probe restores an even sequence. */
	if (busy) g_health_sequence |= 1;
	else if (g_health_sequence & 1) g_health_sequence++;
	irqrestore(flags);
}
