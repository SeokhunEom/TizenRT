#ifndef __MLC_TASK6_NATIVE_QUEUE_H
#define __MLC_TASK6_NATIVE_QUEUE_H

typedef struct dq_queue_s {
	void *head;
} dq_queue_t;

typedef struct sq_entry_s {
	struct sq_entry_s *flink;
} sq_entry_t;

typedef struct sq_queue_s {
	sq_entry_t *head;
	sq_entry_t *tail;
} sq_queue_t;

#endif
