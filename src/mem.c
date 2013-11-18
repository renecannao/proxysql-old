#include "proxysql.h"

mem_block_t * create_mem_block(mem_superblock_t *msb) {
	mem_block_t *mb=g_slice_new(mem_block_t);
//	mb->mem=g_slice_alloc0(msb->size*msb->incremental); // initialize to 0 . If a block is not zero-ed means it is already used/initialized
	if (posix_memalign(&mb->mem, sysconf(_SC_PAGESIZE), msb->size*msb->incremental)) { exit(EXIT_FAILURE); }
	mb->used=g_ptr_array_sized_new(msb->incremental);
	mb->free=g_ptr_array_sized_new(msb->incremental);
	int i;
	for (i=0; i<msb->incremental; i++) {
	void *p=mb->mem+msb->size*i;
		g_ptr_array_add(mb->free,p);
	}
	return mb;
}

void * find_free_mem_block(mem_superblock_t *msb) {
	unsigned int i;
	for (i=0; i<msb->blocks->len; i++) {
		mem_block_t *mb=g_ptr_array_index(msb->blocks,i);
		if (mb->free->len) {
			void *p=g_ptr_array_remove_index_fast(mb->free,0);
			g_ptr_array_add(mb->used,p);
			return p;
		}
	}
	// if we reach here, we couldn't find a free block
	mem_block_t *mb=create_mem_block(msb);
	g_ptr_array_add(msb->blocks,mb);
	return find_free_mem_block(msb);
}

int return_mem_block(mem_superblock_t *msb, void *p) {
	unsigned int i;
	for (i=0; i<msb->blocks->len; i++) {
		mem_block_t *mb=g_ptr_array_index(msb->blocks,i);
		if (g_ptr_array_remove_fast(mb->used, p)==TRUE) {
            g_ptr_array_add(mb->free,p);
            return 0;
        }
    }
	return -1;	
}



void * stack_alloc(shared_trash_stack_t *ts) {
	void *p;
	pthread_mutex_lock(&myds_pool.mutex);
	p=g_trash_stack_pop(&ts->stack);
	if (p) {
		pthread_mutex_unlock(&myds_pool.mutex);
		debug_print("%p\n", p);
		return p;
	}
	void *m;
	if ((m=malloc(ts->size*ts->incremental))==NULL) { exit(EXIT_FAILURE); }
	g_ptr_array_add(ts->blocks,m);
	int i;
	for (i=0; i<ts->incremental; i++) {
		pkt *n=m+i*ts->size;
		g_trash_stack_push(&ts->stack,n);
	}
	p=g_trash_stack_pop(&ts->stack);
	pthread_mutex_unlock(&myds_pool.mutex);
	debug_print("%p\n", p);
	return p;
}

void stack_free(void *p, shared_trash_stack_t *ts) {
	pthread_mutex_lock(&myds_pool.mutex);
	debug_print("%p\n", p);
	g_trash_stack_push(&ts->stack, p);
	pthread_mutex_unlock(&myds_pool.mutex);
}
