#ifndef __MM_LOADABLE_DOMAIN_INTERNAL_H
#define __MM_LOADABLE_DOMAIN_INTERNAL_H

#include <tinyara/config.h>

#include <semaphore.h>

#include <tinyara/mm/mm.h>

enum mm_loadable_domain_state_e {
	MM_DOMAIN_EMPTY = 0,
	MM_DOMAIN_PREPARING,
	MM_DOMAIN_ACTIVE,
	MM_DOMAIN_DYING
};

struct mm_loadable_domain_slot_s {
	enum mm_loadable_domain_state_e state;
	uint32_t generation;
	volatile uint32_t pins;
	struct mm_loadable_domain_registration_s registration;
	char name[MM_LOADABLE_DOMAIN_NAME_MAX];
	sem_t drained;
};

extern struct mm_loadable_domain_slot_s
	g_loadable_domains[MM_LOADABLE_DOMAIN_CAPACITY];

int mm_loadable_domain_lock(bool wait);
void mm_loadable_domain_unlock(void);

#endif
