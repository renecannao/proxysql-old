mem_block_t * create_mem_block(mem_superblock_t *);
void * find_free_mem_block(mem_superblock_t *);
int return_mem_block(mem_superblock_t *, void *);
void * stack_alloc(shared_trash_stack_t *);
void stack_free(void *, shared_trash_stack_t *);
