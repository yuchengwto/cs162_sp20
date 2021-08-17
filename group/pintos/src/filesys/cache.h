#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H


#include "devices/block.h"


void buffer_cache_init(void);
void buffer_cache_read(struct block *block, block_sector_t sector, void *buffer, size_t size, size_t offset);
void buffer_cache_write(struct block *block, block_sector_t sector, const void *buffer, size_t size, size_t offset);
void buffer_cache_flush(struct block *block);
void buffer_cache_destroy(void);




#endif