#include "proxysql.h"


void queue_init(queue_t * q) {
	struct timeval  tv;
	gettimeofday(&tv, NULL);
	unsigned long long l=(tv.tv_sec) * 1000000 + (tv.tv_usec);
//	All the functions in queues must be lock free.
//	If a lock is needed, should be executed by the calling function : mysql_data_stream_init
	q->buffer=find_free_mem_block(&conn_queue_pool);
	gettimeofday(&tv, NULL);
	l=(tv.tv_sec) * 1000000 + (tv.tv_usec) - l;
	__sync_fetch_and_add(&glotimers[TIMER_find_queue],l);	
	q->size=conn_queue_pool.size;
	q->head=0;
	q->tail=0;
}

// destroy a queue
void queue_destroy(queue_t *q) {
	unsigned int i;
	for (i=0; i<conn_queue_pool.blocks->len; i++) {
		mem_block_t *mb=g_ptr_array_index(conn_queue_pool.blocks,i);
		if (g_ptr_array_remove_fast(mb->used,q->buffer)==TRUE) {
			g_ptr_array_add(mb->free,q->buffer);
			return;
		}
	} 
}


inline void queue_zero(queue_t *q) {
	memcpy(q->buffer, q->buffer+q->tail, q->head - q->tail);
	q->head-=q->tail;
	q->tail=0;
}

// returns how much space is available within the queue
inline int queue_available(queue_t *q) {
	return q->size-q->head;
}


// returns how much space is used the queue
inline int queue_data(queue_t *q) {
	return q->head-q->tail;
}

// move the tail forward to indicate that a read was performed
inline void queue_r(queue_t *q, int size) {
	q->tail+=size;
	if (q->tail==q->head) {
		q->head=0;
		q->tail=0;
	}
}

// move the head forward to indicate that a write was performed
inline void queue_w(queue_t *q, int size) {
	q->head+=size;
}

// returns the pointer to the tail: this is where the application must perform reads
inline void *queue_r_ptr(queue_t *q) {
	return q->buffer+q->tail;
}

// returns the pointer to the head: this is where the application must perform writes
inline void *queue_w_ptr(queue_t *q) {
	return q->buffer+q->head;
}

