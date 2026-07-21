#ifndef __TEST_TINYARA_SPINLOCK_H
#define __TEST_TINYARA_SPINLOCK_H

typedef unsigned int spinlock_t;

#define SP_UNLOCKED 0u
#define SP_LOCKED 1u
#define SP_DMB() do { } while (0)
#define SP_DSB() do { } while (0)
#define SP_SEV() do { } while (0)

static inline spinlock_t up_testset(volatile spinlock_t *lock)
{
	spinlock_t previous = *lock;

	*lock = SP_LOCKED;
	return previous;
}

#endif
