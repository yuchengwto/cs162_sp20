#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "cache.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define DIRECT_SIZE 124
#define INDIRECT_SIZE 128
#define FS_LIMIT 1 << 23

enum sector_level{DIRECT, SINGLY_INDIRECT, DOUBLY_INDIRECT};

static bool inode_extend(struct inode_disk *, size_t);
static bool inode_allocate(struct inode_disk *);
static void inode_deallocate(struct inode_disk *);
static enum sector_level determine_level(size_t);
static char sector_zero[BLOCK_SECTOR_SIZE];

struct indirect_block {
  block_sector_t block_ptr[INDIRECT_SIZE];
};


/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    // block_sector_t start;               /* First data sector. */
    block_sector_t direct[DIRECT_SIZE];
    block_sector_t singly_indirect;
    block_sector_t doubly_indirect;
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    // uint32_t unused[125];               /* Not used. */
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    // struct inode_disk data;             /* Inode content. */
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  ASSERT (inode != NULL);
  // if (pos < inode->data.length)
  //   return inode->data.start + pos / BLOCK_SECTOR_SIZE;
  // else
  //   return -1;
  
  struct inode_disk *inode_d = malloc(sizeof(struct inode_disk));
  buffer_cache_read(fs_device, inode->sector, inode_d, BLOCK_SECTOR_SIZE, 0);

  if (pos >= inode_d->length) {
    free(inode_d);
    return -1;
  }

  size_t sector_idx = pos / BLOCK_SECTOR_SIZE;
  enum sector_level level = determine_level(sector_idx);
  block_sector_t block_ptr;
  if (level == DIRECT) {
    block_ptr = inode_d->direct[sector_idx];
  } else if (level == SINGLY_INDIRECT) {
    size_t singly_indirect_idx = sector_idx - DIRECT_SIZE;
    buffer_cache_read(fs_device, inode_d->singly_indirect, &block_ptr, sizeof(block_sector_t), singly_indirect_idx);
  } else {
    size_t doubly_indirect_idx = sector_idx - DIRECT_SIZE - INDIRECT_SIZE;
    size_t singly_indirect_idx = doubly_indirect_idx / INDIRECT_SIZE;
    doubly_indirect_idx = doubly_indirect_idx % INDIRECT_SIZE;
    buffer_cache_read(fs_device, inode_d->doubly_indirect, &block_ptr, sizeof(block_sector_t), singly_indirect_idx);
    buffer_cache_read(fs_device, block_ptr, &block_ptr, sizeof(block_sector_t), doubly_indirect_idx);
  }
  free(inode_d);
  return block_ptr;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);
  ASSERT (length <= FS_LIMIT - sizeof(struct inode_disk));

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;

      success = inode_allocate(disk_inode);
      success = inode_extend(disk_inode, length) && success;

      // if (free_map_allocate (sectors, &disk_inode->start))
      //   {
      //     // block_write (fs_device, sector, disk_inode);
      //     buffer_cache_write(fs_device, sector, disk_inode, BLOCK_SECTOR_SIZE, 0);
      //     if (sectors > 0)
      //       {
      //         static char zeros[BLOCK_SECTOR_SIZE];
      //         size_t i;

      //         for (i = 0; i < sectors; i++) {
      //           // block_write (fs_device, disk_inode->start + i, zeros);
      //           buffer_cache_write(fs_device, disk_inode->start + i, zeros, BLOCK_SECTOR_SIZE, 0);
      //         }
      //       }
      //     success = true;
      //   }
      free (disk_inode);
    }
  return success;
}


/* Find corresponding level of linear sector index. */
static enum sector_level determine_level(size_t sector_idx) {
  static size_t direct_limit = DIRECT_SIZE;
  static size_t singly_indirect_limite = DIRECT_SIZE + INDIRECT_SIZE;

  if (sector_idx < direct_limit) {
    return DIRECT_SIZE;
  } else if (sector_idx < singly_indirect_limite) {
    return SINGLY_INDIRECT;
  } else {
    return DOUBLY_INDIRECT;
  }
}


/* Allocate an inode disk structure. */
static bool inode_allocate(struct inode_disk *inode_d) {
  if (!free_map_allocate(1, &inode_d->singly_indirect)) return false;
  buffer_cache_write(fs_device, inode_d->singly_indirect, sector_zero, BLOCK_SECTOR_SIZE, 0);

  if (!free_map_allocate(1, &inode_d->doubly_indirect)) return false;
  buffer_cache_write(fs_device, inode_d->doubly_indirect, sector_zero, BLOCK_SECTOR_SIZE, 0);

  return true;
}


/* Extend inode disk by length bytes. */
static bool inode_extend(struct inode_disk *inode_d, size_t length) {
  if (inode_d->length > length) {
    /* Only allow grow up now. */
    return false;
  }
  if (length > FS_LIMIT - sizeof(struct inode_disk)) {
    /* Not allow to exceed file system limit. */
    return false;
  }

  size_t cur_sectors = bytes_to_sectors(inode_d->length);
  size_t new_sectors = bytes_to_sectors(length);

  block_sector_t block_ptr;
  for (size_t sector_idx = cur_sectors; sector_idx < new_sectors; ++sector_idx) {
    enum sector_level level = determine_level(sector_idx);
    if (level == DIRECT) {
      /* Direct pointer. */
      if (!free_map_allocate(1, &inode_d->direct[sector_idx])) return false;
    } else if (level == SINGLY_INDIRECT) {
      /* Singly indirect pointer. */
      buffer_cache_read(fs_device, inode_d->singly_indirect, &block_ptr, sizeof(block_sector_t), sector_idx-DIRECT_SIZE);
      if (!free_map_allocate(1, &block_ptr)) return false;
      buffer_cache_write(fs_device, block_ptr, sector_zero, BLOCK_SECTOR_SIZE, 0);
    } else {
      /* Doubly indirect pointer. */
      size_t doubly_sector_idx = sector_idx-DIRECT_SIZE-INDIRECT_SIZE;
      size_t singly_sector_idx = doubly_sector_idx / INDIRECT_SIZE;
      doubly_sector_idx = doubly_sector_idx % INDIRECT_SIZE;
      buffer_cache_read(fs_device, inode_d->doubly_indirect, &block_ptr, sizeof(block_sector_t), singly_sector_idx);
      if (doubly_sector_idx == 0) {
        if (!free_map_allocate(1, &block_ptr)) return false;
        buffer_cache_write(fs_device, block_ptr, sector_zero, BLOCK_SECTOR_SIZE, 0);
      }
      buffer_cache_read(fs_device, block_ptr, &block_ptr, sizeof(block_sector_t), doubly_sector_idx);
      if (!free_map_allocate(1, &block_ptr)) return false;
      buffer_cache_write(fs_device, block_ptr, sector_zero, BLOCK_SECTOR_SIZE, 0);
    }
  }
}

/* Deallocate blocks in inode disk structure. */
static void inode_deallocate(struct inode_disk *inode_d) {
  size_t sectors = bytes_to_sectors(inode_d->length);
  block_sector_t block_ptr;
  for (size_t sector_idx = 0; sector_idx < sectors; ++sector_idx) {
    enum sector_level level = determine_level(sector_idx);
    if (level == DIRECT) {
      /* Direct pointer. */
      free_map_release(&inode_d->direct[sector_idx], 1);
    } else if (level == SINGLY_INDIRECT) {
      /* Singly indirect pointer. */
      size_t singly_sector_idx = sector_idx-DIRECT_SIZE;
      buffer_cache_read(fs_device, inode_d->singly_indirect, &block_ptr, sizeof(block_sector_t), singly_sector_idx);
      free_map_release(block_ptr, 1);
    } else {
      /* Doubly indirect pointer. */
      size_t doubly_sector_idx = sector_idx-DIRECT_SIZE-INDIRECT_SIZE;
      size_t singly_sector_idx = doubly_sector_idx / INDIRECT_SIZE;
      doubly_sector_idx = doubly_sector_idx % INDIRECT_SIZE;
      buffer_cache_read(fs_device, inode_d->doubly_indirect, &block_ptr, sizeof(block_sector_t), singly_sector_idx);
      buffer_cache_read(fs_device, block_ptr, &block_ptr, sizeof(block_sector_t), doubly_sector_idx);
      /* Release the second level pointer. */
      free_map_release(block_ptr, 1);
      if (doubly_sector_idx == INDIRECT_SIZE - 1) {
        /* If last second level poiinter is released, then release the first level pointer. */
        buffer_cache_read(fs_device, inode_d->doubly_indirect, &block_ptr, sizeof(block_sector_t), singly_sector_idx);
        free_map_release(block_ptr, 1);
      }
    }
  }
}


/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          inode_reopen (inode);
          return inode;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  // block_read (fs_device, inode->sector, &inode->data);
  // buffer_cache_read(fs_device, inode->sector, &inode->data, BLOCK_SECTOR_SIZE, 0);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);

      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
          // free_map_release (inode->sector, 1);
          // free_map_release (inode->data.start,
          //                   bytes_to_sectors (inode->data.length));
          struct inode_disk *inode_d = malloc(sizeof(struct inode_disk));
          buffer_cache_read(fs_device, inode->sector, inode_d, BLOCK_SECTOR_SIZE, 0);
          inode_deallocate(inode_d);
          free(inode_d);
        }

      free (inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  /* Determine whether read offset beyond the EOF. */
  size_t necessary_offset = offset + size;
  if (byte_to_sector(inode, necessary_offset) == -1) {
    return bytes_read;
  }
  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      // if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
      //   {
      //     /* Read full sector directly into caller's buffer. */
      //     block_read (fs_device, sector_idx, buffer + bytes_read);
      //   }
      // else
      //   {
      //     /* Read sector into bounce buffer, then partially copy
      //        into caller's buffer. */
      //     if (bounce == NULL)
      //       {
      //         bounce = malloc (BLOCK_SECTOR_SIZE);
      //         if (bounce == NULL)
      //           break;
      //       }
      //     block_read (fs_device, sector_idx, bounce);
      //     memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
      //   }
      buffer_cache_read(fs_device, sector_idx, buffer + bytes_read, chunk_size, sector_ofs);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  /* Extend inode disk if necessary. */
  size_t necessary_offset = offset + size;
  if (byte_to_sector(inode, necessary_offset) == -1) {
    struct inode_disk *inode_d = calloc(1, sizeof(struct inode_disk));
    bool extend_success = inode_extend(inode_d, necessary_offset + 1);
    free(inode_d);
    if (!extend_success) {
      return bytes_written;
    }
  }
  
  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      // if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
      //   {
      //     /* Write full sector directly to disk. */
      //     block_write (fs_device, sector_idx, buffer + bytes_written);
      //   }
      // else
      //   {
      //     /* We need a bounce buffer. */
      //     if (bounce == NULL)
      //       {
      //         bounce = malloc (BLOCK_SECTOR_SIZE);
      //         if (bounce == NULL)
      //           break;
      //       }

      //     /* If the sector contains data before or after the chunk
      //        we're writing, then we need to read in the sector
      //        first.  Otherwise we start with a sector of all zeros. */
      //     if (sector_ofs > 0 || chunk_size < sector_left)
      //       block_read (fs_device, sector_idx, bounce);
      //     else
      //       memset (bounce, 0, BLOCK_SECTOR_SIZE);
      //     memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
      //     block_write (fs_device, sector_idx, bounce);
      //   }

      /* Write to buffer cache. */
      buffer_cache_write(fs_device, sector_idx, buffer + bytes_written, chunk_size, sector_ofs);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  struct inode_disk *inode_d = malloc(sizeof(struct inode_disk));
  buffer_cache_read(fs_device, inode->sector, inode_d, BLOCK_SECTOR_SIZE, 0);
  off_t length = inode_d->length;
  free(inode_d);
  return length;
}
