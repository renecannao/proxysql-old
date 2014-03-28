#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "lutils.h"
#include <stdio.h>

static unsigned int l_near_pow_2 (int n) {
	unsigned int i = 1;
	while (i < n) i <<= 1;
	return i ? i : n;
}


static void *__x_malloc(size_t size) {
	void *m=malloc(size);
	assert(m);
	return m;
}

static void * __x_memalign(size_t size) {
	int rc;
	void *m;
	rc=posix_memalign(&m, sysconf(_SC_PAGESIZE), size);
	assert(rc==0);
	return m;
}


static void l_ptr_array_expand(LPtrArray *array, unsigned int more) {
	if ( (array->len+more) > array->size ) {
		unsigned int new_size=l_near_pow_2(array->len+more);
		void *new_pdata=l_alloc(new_size*sizeof(void *));
		if (array->pdata) {
			memcpy(new_pdata,array->pdata,array->size*sizeof(void *));
			l_free(array->size*sizeof(void *),array->pdata);
		}
		array->size=new_size;
		array->pdata=new_pdata;
	}
}


LPtrArray *l_ptr_array_sized_new(unsigned int size) {
	LPtrArray *array=l_alloc(sizeof(LPtrArray));
	array->pdata=NULL;
	array->len=0;
	array->size=0;
	if (size) {
		l_ptr_array_expand(array, size);
	}
	return array;
}

LPtrArray *l_ptr_array_new() {
	return l_ptr_array_sized_new(0);
}

void l_ptr_array_add(LPtrArray *array, void *p) {
	if (array->len==array->size) {
		l_ptr_array_expand(array,1);
	}
	array->pdata[array->len++]=p;
}

void * l_ptr_array_remove_index(LPtrArray *array, unsigned int i) {
	void *r=array->pdata[i];
	if (i != (array->len-1)) {
		int j;
		for (j=i; j<array->len-1; j++) {
			array->pdata[j]=array->pdata[j+1];
		}
	}
	array->len--;
	return r;
}

void * l_ptr_array_remove_index_fast (LPtrArray *array, unsigned int i) {
	void *r=array->pdata[i];
	if (i != (array->len-1))
    array->pdata[i]=array->pdata[array->len-1];
	array->len--;
	return r;
}

int l_ptr_array_remove_fast(LPtrArray *array, void *p) {
	unsigned int i;
	unsigned len=array->len;
	for (i=0; i<len; i++) {
		if (array->pdata[i]==p) {
			l_ptr_array_remove_index_fast(array, i);
			return 1;
		}
	}
	return 0;
}


static void __add_mem_block(l_sfc *sfc, void *m) {
	void *nmp=__x_malloc(sizeof(void *)*(sfc->blocks_cnt+1));
	if (sfc->mem_blocks) {
		memcpy(nmp,sfc->mem_blocks,sizeof(void *)*(sfc->blocks_cnt));
		free(sfc->mem_blocks);
	}
	sfc->mem_blocks=nmp;
	sfc->mem_blocks[sfc->blocks_cnt++]=m;
}

l_sfp * l_mem_init() {
	l_sfp *s=__x_malloc(sizeof(l_sfp));
	int i;
	for (i=0; i<L_SFP_ARRAY_LEN; i++) {
		s->sfc[i].stack=NULL;
		s->sfc[i].mem_blocks=NULL;
		s->sfc[i].elem_size=L_SFC_MIN_ELEM_SIZE * (1 << i) ;
		s->sfc[i].blocks_cnt=0;
	}
	return s;
}



void * __l_alloc(l_sfp *sfp, size_t size) {
	if (size>L_SFC_MAX_ELEM_SIZE) {
		return __x_malloc(size);
	}
	void *p;
	int i;
	i=L_SFP_ARRAY_LEN-1;
	if (size<=L_SFC_MID_ELEM_SIZE) i=L_SFP_ARRAY_MID-1;
	for ( ; i>=0 ; i-- ) {
		if (size*2>sfp->sfc[i].elem_size || i==0) {
			p=l_stack_pop(&sfp->sfc[i].stack);
			if (p) {
				return p;
			}
			void *m=__x_memalign(L_SFC_MEM_BLOCK_SIZE);
			__add_mem_block(&sfp->sfc[i],m);
			int j;
			for (j=0; j<L_SFC_MEM_BLOCK_SIZE/sfp->sfc[i].elem_size; j++) {
				void *n=m+j*sfp->sfc[i].elem_size;
				l_stack_push(&sfp->sfc[i].stack,n);
			}
			p=l_stack_pop(&sfp->sfc[i].stack);
			return p;
		}
	}
	return NULL; // should never reach here
}

void * l_alloc(size_t size) {
	return __l_alloc(__thr_sfp,size);
}


void * l_alloc0(size_t size) {
		void *p=l_alloc(size);
		memset(p,0,size);
		return p;
}

void __l_free(l_sfp *sfp, size_t size, void *p) {
	if (size>L_SFC_MAX_ELEM_SIZE) {
		free(p);
		return;
	}
	int i;
	for (i=L_SFP_ARRAY_LEN-1 ; i>=0 ; i-- ) {
		if (size*2>sfp->sfc[i].elem_size || i==0) {
			l_stack_push(&sfp->sfc[i].stack,p);
			return;
		}	
	}
}

void l_free(size_t size, void *p) {
	__l_free(__thr_sfp,size,p);
}
