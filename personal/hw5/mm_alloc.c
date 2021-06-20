/*
 * mm_alloc.c
 */

#include "mm_alloc.h"

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>




static void insert_block(struct block *prev_block, struct block *insert_block);
static void zero_block(struct block *block);
static struct block *merge_block(struct block *down, struct block *up);




void* mm_malloc(size_t size)
{
  //TODO: Implement malloc
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
        // Free block is large enough to split into two new block
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
  size_t curBrk = sbrk(0);
  sbrk(sizeof(struct block) + size);
  cur_block = (struct block *)curBrk;
  cur_block->size = size;
  cur_block->free = 0;
  cur_block->prev = pre_block;
  cur_block->next = NULL;
  zero_block(cur_block);
  return &cur_block->block_content;
}

void* mm_realloc(void* ptr, size_t size)
{
  //TODO: Implement realloc
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

  struct block *curr_block = ptr - sizeof(struct block);
  if (curr_block->size >= size) {
    curr_block->size = size;
    return ptr;
  } else {
    void *n_ptr = mm_malloc(size);
    struct block *n_block = n_ptr - sizeof(struct block);
    memcpy(&n_block->block_content, &curr_block->block_content, curr_block->size);
    mm_free(ptr);
    return n_ptr;
  }
}

void mm_free(void* ptr)
{
  //TODO: Implement free
  if (ptr == NULL) {
    return;
  }

  struct block *curr_block = ptr - sizeof(struct block);
  struct block *prev_block = curr_block->prev;
  struct block *next_block = curr_block->next;
  if (prev_block != NULL) {
    if (prev_block->free == 1) {
      curr_block = merge_block(prev_block, curr_block);
    }
  }

  if (next_block != NULL) {
    if (next_block->free == 1) {
      curr_block = merge_block(curr_block, next_block);
    }
  }
}








static void insert_block(struct block *prev_block, struct block *insert_block) {
  struct block *next_block = prev_block->next;
  if (next_block == NULL) {
    prev_block->next = insert_block;
    insert_block->prev = prev_block;
    insert_block->next = NULL;
  } else {
    prev_block->next = insert_block;
    insert_block->prev = prev_block;
    insert_block->next = next_block;
  }
}


static void zero_block(struct block *block) {
  memset(&block->block_content, 0, block->size);
}


static struct block *merge_block(struct block *down, struct block *up) {
  down->size = (size_t)&up->block_content - (size_t)&down->block_content + up->size;
  down->free = 1;
  down->next = up->next;
  zero_block(down);
  return down;
}
