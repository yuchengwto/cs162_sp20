#include <stdlib.h>

struct block {
    size_t size;
    int free;
    struct block *prev;
    struct block *next;
    char block_content[0];
};

static struct block *base_block = NULL;
static void *start_heap = NULL;

static void* mm_malloc(size_t size);
static void* mm_realloc(void* ptr, size_t size);
static void mm_free(void* ptr);
static void insert_block(struct block *pre_block, struct block *insert_block);
static void zero_block(struct block *block);
static void clear_block(struct block *block);
static struct block *merge_block(struct block *down, struct block *up);


void*
malloc (size_t size)
{
  /* Homework 6, Part B: YOUR CODE HERE */
  return mm_malloc(size);
}

void free (void* ptr)
{
  /* Homework 6, Part B: YOUR CODE HERE */
  mm_free(ptr);
}

void* calloc (size_t nmemb, size_t size)
{
  /* Homework 6, Part B: YOUR CODE HERE */
  return mm_malloc(nmemb * size);
}

void* realloc (void* ptr, size_t size)
{
  /* Homework 6, Part B: YOUR CODE HERE */
  return mm_realloc(ptr, size);
}




static void* mm_malloc(size_t size)
{
  if (size == 0) {return NULL;}

  if (base_block == NULL) {
    start_heap = sbrk(0);
    sbrk(sizeof(struct block) + size);
    base_block = (struct block *)start_heap;
    base_block->size = size;
    base_block->free = 0;
    base_block->prev = NULL;
    base_block->next = NULL;
    zero_block(base_block);
    return &base_block->block_content;
  }

  struct block *pre_block = base_block->prev;
  struct block *cur_block = base_block;
  do
  {
    if (cur_block->free == 1 && cur_block->size >= size) {
      // Find one block successfully
      if (cur_block->size - size >= sizeof(struct block)) {
        // Free block is large enough to split into two new blocks
        struct block *additional_block = cur_block->block_content + size;
        additional_block->size = cur_block->size - size - sizeof(struct block);
        additional_block->free = 1;
        zero_block(additional_block);
        cur_block->size = size;
        cur_block->free = 0;
        zero_block(cur_block);
        insert_block(cur_block, additional_block);
      } else {
        // Not enough to allocate an additional block
        cur_block->size = size;
        cur_block->free = 0;
        zero_block(cur_block);
      }
      return &cur_block->block_content;
    }
    pre_block = cur_block;
    cur_block = cur_block->next;
  } while (cur_block != NULL);
  
  // Create new memory block
  void *curBrk = sbrk(sizeof(struct block) + size);
  if (curBrk == -1) {
    return NULL;
  }
  cur_block = (struct block *) curBrk;
  cur_block->size = size;
  cur_block->free = 0;
  cur_block->prev = pre_block;
  cur_block->next = NULL;
  zero_block(cur_block);
  return &cur_block->block_content;
}

static void* mm_realloc(void* ptr, size_t size)
{
  if (ptr == NULL) {
    if (size == 0) {
      return mm_malloc(0);
    } else {
      return mm_malloc(size);
    }
  }

  if (size == 0) {
    mm_free(ptr);
    return NULL;
  }

  struct block *cur_block = (struct block *)(ptr - sizeof(struct block));
  // if (cur_block->size >= size) {
  //   cur_block->size = size;
  //   return ptr;
  // } else {
    void *n_ptr = mm_malloc(size);
    if (n_ptr == NULL) {
      return n_ptr;
    }
    struct block *n_block = (struct block *)(n_ptr - sizeof(struct block));
    memcpy(n_block->block_content, cur_block->block_content, size);
    mm_free(ptr);

    return n_ptr;
  // }
}

static void mm_free(void* ptr)
{
  if (ptr == NULL) {
    return;
  }

  struct block *cur_block = (struct block *) (ptr - sizeof(struct block));
  struct block *pre_block = cur_block->prev;
  struct block *nxt_block = cur_block->next;
  if (pre_block != NULL) {
    if (pre_block->free == 1) {
      cur_block = merge_block(pre_block, cur_block);
    }
  }

  if (nxt_block != NULL) {
    if (nxt_block->free == 1) {
      cur_block = merge_block(cur_block, nxt_block);
    }
  }

  clear_block(cur_block);
}


static void insert_block(struct block *pre_block, struct block *insert_block) {
  struct block *nxt_block = pre_block->next;
  if (nxt_block == NULL) {
    pre_block->next = insert_block;
    insert_block->prev = pre_block;
    insert_block->next = NULL;
  } else {
    pre_block->next = insert_block;
    insert_block->prev = pre_block;
    insert_block->next = nxt_block;
  }
}


static void zero_block(struct block *block) {
  memset(block->block_content, 0, block->size);
}

static void clear_block(struct block *block) {
  block->free = 1;
  zero_block(block);
}

static struct block *merge_block(struct block *down, struct block *up) {
  down->size = (void *) up->block_content - (void *) down->block_content + up->size;
  down->next = up->next;
  return down;
}
