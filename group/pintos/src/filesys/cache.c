#include <hash.h>
#include <bitmap.h>
#include <string.h>
#include "filesys/cache.h"
#include "threads/malloc.h"
#include "threads/synch.h"




#define MAX_CACHE_SECTORS 64


/* Sector saved in buffer_cache, including controll information. */
struct cache_sector {
    block_sector_t sector_idx;          /* Sector ID. */
    size_t clock_idx;                   /* Index same with global clock_idx, meanwhile pointing to buffer content. */
    struct hash_elem elem;
    bool dirty;
};



static struct hash buffer_cache;        /* Global buffer cash, this structure created on stack of kernel thread. */
static void *buffer_base;               /* Sector content saved start at this point, up to 64 sectors, also as 32KB. */
static size_t clock_idx;                /* Clock pointer, start at 0, up to 63. */
static struct bitmap *refbits;
static struct cache_sector *cache_sectors[MAX_CACHE_SECTORS];
static struct lock sector_locks[MAX_CACHE_SECTORS];



// read时，如果cache中保存有该sector，将该sector的refbit值为1，然后返回该sector的内容。
// 如果没有，则从磁盘中读取后，保存至cache中，refbit置为1，再从cache中读取。
// write时，如果cache中有该sector，修改该sector的content并将其refbit置为1。
// 如果没有，则插入一个sector，refbit置为1。
// 每当插入sector到cache时，插入位置应为clock_idx时钟指针位置，即时钟指针应在插入前指向下一个插入处，即下一个refbit为0的位置。
// 插入操作时，会将该位置原有的数据evict，写入到磁盘上。
//
// 当找不到refbit为0的位置时，将所有bit置为0，然后将clock_idx置为0，即开始位置，然后再进行插入操作。
// 
// 由于读写可能同时发生，为了防止脏读或者幻读出现，需要在所有读写操作上加锁。
//
// 每个sector需要一个独立的锁，而不是全局锁，来保持并发性。但是有对refbit和buffer_cache的访问时，还是需要各自独立加锁。



static unsigned hash_func(const struct hash_elem *e, void *aux UNUSED) {
    return hash_entry(e, struct cache_sector, elem)->sector_idx;
}

static bool less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED) {
    return hash_func(a, NULL) < hash_func(b, NULL);
}

static struct hash_elem *buffer_cache_find(size_t sector_idx);
static void buffer_cache_insert(struct cache_sector *entry);
static void buffer_cache_evict(struct block *block, struct cache_sector *entry);
static void update_clock_idx();
static void* idx2addr(size_t cache_idx);

static void read_from_buffer(struct block *block, block_sector_t sector, void *buffer, size_t size, size_t offset);
static void read_from_disk(struct block *block, block_sector_t sector, void *buffer, const size_t clock_hand, size_t size, size_t offset);
static void write_exist_buffer(struct block *block, block_sector_t sector, const void *buffer, size_t size, size_t offset);
static void write_new_buffer(struct block *block, block_sector_t sector, const void *buffer, const size_t clock_hand, size_t size, size_t offset);


void buffer_cache_init() {
    hash_init(&buffer_cache, hash_func, less_func, NULL);
    buffer_base = malloc(MAX_CACHE_SECTORS * BLOCK_SECTOR_SIZE);            /* Allocate 8 continuous pages memory on heap, buffer base point to start of these pages. */
    clock_idx = 0;
    refbits = bitmap_create(MAX_CACHE_SECTORS);

    for (size_t i = 0; i != MAX_CACHE_SECTORS; ++i) {
        lock_init(&sector_locks[i]);
    }
}


void buffer_cache_destroy() {
    hash_destroy(&buffer_cache, NULL);
    bitmap_destroy(refbits);
    free(buffer_base);

    for (size_t i = 0; i != MAX_CACHE_SECTORS; ++i) {
        free(cache_sectors[i]);
    }
}


void buffer_cache_read(struct block *block, block_sector_t sector, void *buffer, size_t size, size_t offset) {

    struct hash_elem *e = buffer_cache_find(sector);
    // printf("%d\t%p\n", sector, buffer);
    if (e != NULL) {
        // printf("%p\n", e);
        struct cache_sector *entry = hash_entry(e, struct cache_sector, elem);
        lock_acquire(&sector_locks[entry->clock_idx]);
        /* Determine again after get lock. */
        e = buffer_cache_find(sector);
        if (e == NULL) {
            read_from_disk(block, sector, buffer, entry->clock_idx, size, offset);
        } else {
            read_from_buffer(block, sector, buffer, size, offset);
        }
        lock_release(&sector_locks[entry->clock_idx]);
    } else {
        // printf("%p\n", e);
        update_clock_idx();
        size_t clock_hand = clock_idx;
        lock_acquire(&sector_locks[clock_hand]);
        e = buffer_cache_find(sector);
        if (e == NULL) {
            read_from_disk(block, sector, buffer, clock_hand, size, offset);
        } else {
            read_from_buffer(block, sector, buffer, size, offset);
        }
        lock_release(&sector_locks[clock_hand]);
    }
}


void buffer_cache_write(struct block *block, block_sector_t sector, const void *buffer, size_t size, size_t offset) {
    struct hash_elem *e = buffer_cache_find(sector);
    if (e != NULL) {
        struct cache_sector *entry = hash_entry(e, struct cache_sector, elem);
        lock_acquire(&sector_locks[entry->clock_idx]);
        e = buffer_cache_find(sector);
        if (e == NULL) {
            write_new_buffer(block, sector, buffer, entry->clock_idx, size, offset);
        } else {
            write_exist_buffer(block, sector, buffer, size, offset);
        }
        lock_release(&sector_locks[entry->clock_idx]);
    } else {
        update_clock_idx();
        size_t clock_hand = clock_idx;
        lock_acquire(&sector_locks[clock_hand]);
        e = buffer_cache_find(sector);
        if (e == NULL) {
            write_new_buffer(block, sector, buffer, clock_hand, size, offset);
        } else {
            write_exist_buffer(block, sector, buffer, size, offset);
        }
        lock_release(&sector_locks[clock_hand]);
    }
}


void buffer_cache_flush(struct block *block) {
    for (size_t i = 0; i != MAX_CACHE_SECTORS; ++i) {
        lock_acquire(&sector_locks[i]);
    }

    /* Flush all dirty sector in cache onto disk. */
    for (size_t i = 0; i != MAX_CACHE_SECTORS; ++i) {
        struct cache_sector *entry = cache_sectors[i];
        if (entry != NULL && entry->dirty) {
            block_write(block, entry->sector_idx, idx2addr(i));
        }
    }

    for (size_t i = 0; i != MAX_CACHE_SECTORS; ++i) {
        lock_release(&sector_locks[i]);
    }
}

static void update_clock_idx() {
    /* Update clock index before modify the cache. */
    if (bitmap_all(refbits, 0, MAX_CACHE_SECTORS)) {
        /* No valid space for clock pointer, flush all clock. */
        bitmap_set_all(refbits, false);
        clock_idx = 0;
        return;
    }

    /* Move clock pointer. */
    /* Find the first zero bit in refbits. */
    while (bitmap_test(refbits, clock_idx)) {
        clock_idx = ((clock_idx + 1) % MAX_CACHE_SECTORS);
        return;
    }
}

static struct hash_elem *buffer_cache_find(size_t sector_idx) {
    struct cache_sector *entry = malloc(sizeof(struct cache_sector));
    entry->sector_idx = sector_idx;
    struct hash_elem *e = hash_find(&buffer_cache, &entry->elem);
    return e;
}



static void buffer_cache_insert(struct cache_sector *entry) {
    hash_replace(&buffer_cache, &entry->elem);
    bitmap_set(refbits, entry->clock_idx, true);
    cache_sectors[entry->clock_idx] = entry;
}


static void* idx2addr(size_t cache_idx) {
    return buffer_base + (cache_idx * BLOCK_SECTOR_SIZE);
}


static void buffer_cache_evict(struct block *block, struct cache_sector *entry) {
    if (entry == NULL) {
        return;
    }

    /* Dump entry sector to disk. */
    size_t cache_idx = entry->clock_idx;
    void *sector_start = idx2addr(cache_idx);
    if (entry->dirty) {
        block_write(block, entry->sector_idx, sector_start);
    }
    
    /* Delete entry in hash structure. */
    hash_delete(&buffer_cache, &entry->elem);

    /* Free cache_sector in array. */
    free(cache_sectors[cache_idx]);
    cache_sectors[cache_idx] = NULL;

    /* Clear heap buffer content. */
    memset(sector_start, 0, BLOCK_SECTOR_SIZE);
}



static void read_from_buffer(struct block *block, block_sector_t sector, void *buffer, size_t size, size_t offset) {
    struct hash_elem *e = buffer_cache_find(sector);
    struct cache_sector *entry = hash_entry(e, struct cache_sector, elem);
    /* Set access sector to 1. */
    bitmap_set(refbits, entry->clock_idx, true);
    /* Read from cache to buffer. */
    void *sector_start = idx2addr(entry->clock_idx);
    memcpy(buffer, sector_start + offset, size);
}


static void read_from_disk(struct block *block, block_sector_t sector, void *buffer, const size_t clock_hand, size_t size, size_t offset) {
    /* No reading sector exists in cache. */
    struct cache_sector *entry = malloc(sizeof(struct cache_sector));
    entry->sector_idx = sector;
    entry->clock_idx = clock_hand;
    entry->dirty = false;

    /* Evict sector. */
    struct cache_sector *to_evict = cache_sectors[clock_hand];
    buffer_cache_evict(block, to_evict);
    /* Insert this new sector entry to hashmap. */
    buffer_cache_insert(entry);
    /* Write to cache from disk. */
    void *sector_start = idx2addr(clock_hand);
    block_read(block, sector, sector_start);
    /* Read from cache to buffer. */
    memcpy(buffer, sector_start + offset, size);
}



static void write_exist_buffer(struct block *block, block_sector_t sector, const void *buffer, size_t size, size_t offset) {
    struct hash_elem *e = buffer_cache_find(sector);
    struct cache_sector *entry = hash_entry(e, struct cache_sector, elem);
    entry->dirty = true;
    bitmap_set(refbits, entry->clock_idx, true);
    /* Write to cache from buffer. */
    void *sector_start = idx2addr(entry->clock_idx);
    memcpy(sector_start + offset, buffer, size);
}


static void write_new_buffer(struct block *block, block_sector_t sector, const void *buffer, const size_t clock_hand, size_t size, size_t offset) {
    struct cache_sector *entry = malloc(sizeof(struct cache_sector));
    entry->sector_idx = sector;
    entry->clock_idx = clock_hand;
    entry->dirty = true;
    
    /* Evict sector. */
    struct cache_sector *to_evict = cache_sectors[clock_hand];
    buffer_cache_evict(block, to_evict);
    /* Insert this new sector entry to hashmap. */
    buffer_cache_insert(entry);
    /* Write to cache from buffer. */
    void *sector_start = idx2addr(clock_hand);
    memcpy(sector_start + offset, buffer, size);
}
