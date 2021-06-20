/*
 * mm_alloc.h
 *
 * Exports a clone of the interface documented in "man 3 malloc".
 */

#pragma once

#ifndef _malloc_H_
#define _malloc_H_

#include <stdlib.h>


void* mm_malloc(size_t size);
void* mm_realloc(void* ptr, size_t size);
void mm_free(void* ptr);

//TODO: Add any implementation details you might need to this file
struct block {
    size_t size;
    int free;
    struct block *prev;
    struct block *next;
    char block_content[0];
};

struct block *base_block = NULL;
void *start_heap = NULL;

#endif
