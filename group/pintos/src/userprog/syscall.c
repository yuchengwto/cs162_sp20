#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

#include "lib/kernel/stdio.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/directory.h"
#include "filesys/free-map.h"
#include "process.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "string.h"
#include "lib/user/stdlib.h"
#include "lib/user/syscall.h"
#include "filesys/inode.h"



static void syscall_handler (struct intr_frame *);
static void sys_exit(struct intr_frame *, uint32_t *);
static void sys_practice(struct intr_frame *, uint32_t *);
static void sys_exec(struct intr_frame *, uint32_t *);
static void sys_wait(struct intr_frame *, uint32_t *);
static void sys_create(struct intr_frame *, uint32_t *);
static void sys_remove(struct intr_frame *, uint32_t *);
static void sys_open(struct intr_frame *, uint32_t *);
static void sys_close(struct intr_frame *, uint32_t *);
static void sys_filesize(struct intr_frame *, uint32_t *);
static void sys_read(struct intr_frame *, uint32_t *);
static void sys_write(struct intr_frame *, uint32_t *);
static void sys_seek(struct intr_frame *, uint32_t *);
static void sys_tell(struct intr_frame *, uint32_t *);
static void sys_chdir(struct intr_frame *, uint32_t*);
static void sys_mkdir(struct intr_frame *, uint32_t*);
static void sys_readdir(struct intr_frame *, uint32_t*);
static void sys_isdir(struct intr_frame *, uint32_t*);
static void sys_inumber(struct intr_frame *, uint32_t*);
static void sys_halt();

static void error(struct intr_frame *);
static int get_user (const uint8_t *);
static bool put_user (uint8_t *, uint8_t);
static int get_user_byte(const uint8_t *, struct intr_frame *);
static void *get_user_ptr(const uint8_t *, struct intr_frame *);
static void put_user_byte(uint8_t *, uint8_t, struct intr_frame *);
static int get_user_int(const uint8_t *, struct intr_frame *);
static struct fnode *get_fn_from_fd (int);


void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  uint32_t* args = ((uint32_t*) f->esp);
  int cmd = get_user_byte((const uint8_t *)args, f);

  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */

  // printf("System call number: %d\n", args[0]);

  switch (cmd)
  {
  case SYS_EXIT:
    sys_exit(f, args);
    break;
  case SYS_EXEC:
    sys_exec(f, args);
    break;
  case SYS_WAIT:
    sys_wait(f, args);
    break;
  case SYS_PRACTICE:
    sys_practice(f, args);
    break;
  case SYS_CREATE:
    sys_create(f, args);
    break;
  case SYS_REMOVE:
    sys_remove(f, args);
    break;
  case SYS_OPEN:
    sys_open(f, args);
    break;
  case SYS_CLOSE:
    sys_close(f, args);
    break;
  case SYS_FILESIZE:
    sys_filesize(f, args);
    break;
  case SYS_READ:
    sys_read(f, args);
    break;
  case SYS_WRITE:
    sys_write(f, args);
    break;
  case SYS_SEEK:
    sys_seek(f, args);
    break;
  case SYS_TELL:
    sys_tell(f, args);
    break;
  case SYS_CHDIR:
    sys_chdir(f, args);
    break;
  case SYS_MKDIR:
    sys_mkdir(f, args);
    break;
  case SYS_READDIR:
    sys_readdir(f, args);
    break;
  case SYS_ISDIR:
    sys_isdir(f, args);
    break;
  case SYS_INUMBER:
    sys_inumber(f, args);
    break;
  case SYS_HALT:
    sys_halt();
    break;
  
  default:
    error(f);
  }
}


static void sys_chdir(struct intr_frame *f UNUSED, uint32_t* args) {
  const char *path = get_user_ptr((const uint8_t *)(args+1), f);
  bool success = false;

  struct dir *dir;
  char name[NAME_MAX + 1];
  struct inode *node;

  success = parse_path(dir, name, path);
  success = dir_lookup(dir, name, &node) && success;
  dir_close(dir);

  success = inode_isdir(node) && success;
  thread_current()->cwd = inode_get_inumber(node);
  f->eax = success;
}

static void sys_mkdir(struct intr_frame *f UNUSED, uint32_t* args) {
  const char *path = get_user_ptr((const uint8_t *)(args+1), f);
  bool success = false;

  struct dir *dir;
  char name[NAME_MAX + 1];
  block_sector_t dir_sector;

  success = parse_path(dir, name, path);
  struct inode *parent = dir_get_inode(dir);
  block_sector_t parent_n = inode_get_inumber(parent);
  dir_close(dir);

  success = free_map_allocate(1, &dir_sector) && success;
  success = dir_create(dir_sector, 1, parent_n) && success;
  f->eax = success;
}

static void sys_readdir(struct intr_frame *f UNUSED, uint32_t* args) {
  int fd = get_user_byte((const uint8_t *)(args+1), f);
  char *name = get_user_ptr((const uint8_t *)(args+2), f);

  struct fnode *fn = get_fn_from_fd(fd);
  if (fn == NULL) error(f);
  struct file *fp = fn->file_ptr;
  struct inode *node = file_get_inode(fp);
  if (!inode_isdir(node)) error(f);
  struct dir *dir = dir_open(node);
  f->eax = dir_readdir(dir, name);
}

static void sys_isdir(struct intr_frame *f UNUSED, uint32_t* args) {
  int fd = get_user_byte((const uint8_t *)(args+1), f);
  struct fnode *fn = get_fn_from_fd(fd);
  if (fn == NULL) error(f);
  
  struct file *fp = fn->file_ptr;
  struct inode *node = file_get_inode(fp);
  f->eax = inode_isdir(node);
}

static void sys_inumber(struct intr_frame *f UNUSED, uint32_t* args) {
  int fd = get_user_byte((const uint8_t *)(args+1), f);
  struct fnode *fn = get_fn_from_fd(fd);
  if (fn == NULL) error(f);
  struct file *fp = fn->file_ptr;
  struct inode *node = file_get_inode(fp);
  f->eax = inode_get_inumber(node);
}

static void sys_halt() {
  shutdown_power_off();
}

static void sys_create(struct intr_frame *f UNUSED, uint32_t* args) {
  const char *filename = get_user_ptr((const uint8_t *)(args+1), f);
  unsigned initial_size = get_user_byte((const uint8_t *)(args+2), f);
  f->eax = filesys_create(filename, initial_size);
}

static void sys_remove(struct intr_frame *f UNUSED, uint32_t* args) {
  const char *filename = get_user_ptr((const uint8_t *)(args+1), f);
  f->eax = filesys_remove(filename);
}

static void sys_open(struct intr_frame *f UNUSED, uint32_t* args) {
  const char *filename = get_user_ptr((const uint8_t *)(args+1), f);
  struct file *fp = filesys_open(filename);
  if (fp == NULL) {
    f->eax = -1;
    return;
  }

  /* Add opened file to file_list of current thread. */
  struct list *list_f = &thread_current()->file_list;
  struct fnode *fn;
  fn = (struct fnode *)malloc(sizeof(struct fnode));
  fn->fd = thread_current()->next_fd++;
  fn->file_ptr = fp;
  fn->file_name = filename;
  fn->file_lock = (struct lock *)malloc(sizeof(struct lock));
  lock_init(fn->file_lock);
  list_push_back(list_f, &fn->elem);
  f->eax = fn->fd;
}

static void sys_close(struct intr_frame *f UNUSED, uint32_t* args) {
  int fd = get_user_byte((const uint8_t *)(args+1), f);

  // Remove file from file_list of current thread
  struct list *list_f = &thread_current()->file_list;
  struct list_elem *e = list_begin(list_f);
  for (; e != list_end(list_f); e = list_next(e)) {
    struct fnode *fn = list_entry(e, struct fnode, elem);
    if (fn->fd == fd) {
      file_close(fn->file_ptr);
      list_remove(e);
      free(fn->file_lock);
      free(fn);
      return; // Must return here since e has no next list_elem after list_remove
    }
  }
}


static void sys_filesize(struct intr_frame *f UNUSED, uint32_t* args) {
  int fd = get_user_byte((const uint8_t *)(args+1), f);
  struct fnode *fn = get_fn_from_fd(fd);
  if (fn == NULL) {
    error(f);
  }
  struct file *fp = fn->file_ptr;
  f->eax = file_length(fp);
}


static void sys_read(struct intr_frame *f UNUSED, uint32_t* args) {
  int fd = get_user_byte((const uint8_t *)(args+1), f);
  void *buffer = get_user_ptr((const uint8_t *)(args+2), f);
  unsigned size = get_user_byte((const uint8_t *)(args+3), f);

  if (fd == 0) {
    f->eax = input_getc();
  } else if (fd < 0 || fd == 1) {
    error(f);
  } else {
    struct fnode *fn = get_fn_from_fd(fd);
    if (fn == NULL) {
      error(f);
    }
    struct file *fp = fn->file_ptr;
    if (inode_isdir(file_get_inode(fp))) {
      error(f);
    }
    f->eax = file_read(fp, buffer, size);
  }
}


static void sys_write(struct intr_frame *f UNUSED, uint32_t* args) {
  int fd = get_user_byte((const uint8_t *)(args+1), f);
  const void *buffer = get_user_ptr((const uint8_t *)(args+2), f);
  int size = get_user_int((const uint8_t *)(args+3), f);

  if (fd == 1) {  // stdout
    putbuf(buffer, size);
    f->eax = size;
  } else if (fd <=0) {  // stdin and stderr
    error(f);
  } else {
    struct fnode *fn = get_fn_from_fd(fd);
    if (fn == NULL) {
      error(f);
    }
    struct file *fp = fn->file_ptr;
    if (inode_isdir(file_get_inode(fp))) {
      error(f);
    }
    lock_acquire(fn->file_lock);
    f->eax = file_write(fp, buffer, size);
    lock_release(fn->file_lock);
  }
}


static void sys_seek(struct intr_frame *f UNUSED, uint32_t* args) {
  int fd = get_user_byte((const uint8_t *)(args+1), f);
  unsigned position = get_user_byte((const uint8_t *)(args+2), f);
  struct fnode *fn = get_fn_from_fd(fd);
  if (fn == NULL) {
    error(f);
  }
  struct file *fp = fn->file_ptr;
  lock_acquire(fn->file_lock);
  file_seek(fp, position);
  lock_release(fn->file_lock);
}


static void sys_tell(struct intr_frame *f UNUSED, uint32_t* args) {
  int fd = get_user_byte((const uint8_t *)(args+1), f);
  struct fnode *fn = get_fn_from_fd(fd);
  if (fn == NULL) {
    error(f);
  }
  struct file *fp = fn->file_ptr;
  f->eax = file_tell(fp);
}


static void sys_exit(struct intr_frame *f UNUSED, uint32_t* args) {
  f->eax = get_user_byte((const uint8_t *)(args+1), f);
  thread_current()->pn->exit_status = f->eax;
  printf ("%s: exit(%d)\n", thread_current()->name, f->eax);
  thread_exit ();
}

static void sys_practice(struct intr_frame *f UNUSED, uint32_t* args) {
  f->eax = get_user_byte((const uint8_t *)(args+1), f) + 1;
}

static void sys_exec(struct intr_frame *f UNUSED, uint32_t* args) {
  const char *filename = get_user_ptr((const uint8_t *)(args+1), f);
  // Check the filename string is valid or not
  int idx = 0;
  while (get_user_byte((const uint8_t *)(filename+idx++), f) != '\0') {}

  size_t filenameLen = strcspn(filename, " ") + 1;
  char tmp[filenameLen];
  strlcpy(tmp, filename, filenameLen);
  struct file *fp = filesys_open(tmp);
  if (fp == NULL) {
    f->eax = -1;
    return;
  }
  file_close(fp);
  f->eax = process_execute(filename);
}


static void sys_wait(struct intr_frame *f UNUSED, uint32_t* args) {
  pid_t pid = get_user_byte((const uint8_t *)(args+1), f);
  f->eax = process_wait(pid);
}


static int pow16(int n) {
  int r = 1;
  while (n--) {
    r *= 16;
  }
  return r;
}

static void *get_user_ptr(const uint8_t *uaddr, struct intr_frame *f) {
  size_t idx = 0;
  int ptr = 0; 
  for (; idx != 4; ++idx) {
    ptr += get_user_byte(uaddr+idx, f) * pow16(idx*2);
  }
  if (ptr == NULL) {
    error(f);
  }
  // Check the get pointer is valid or not
  get_user_byte((const uint8_t *) ptr, f);
  return ptr;
}

static int get_user_int(const uint8_t *uaddr, struct intr_frame *f) {
  size_t idx = 0;
  int res = 0; 
  for (; idx != 4; ++idx) {
    res += get_user_byte(uaddr+idx, f) * pow16(idx*2);
  }
  return res;
}

static int get_user_byte(const uint8_t *uaddr, struct intr_frame *f) {
  if (is_kernel_vaddr(uaddr)) {
    error(f);
  }
  int result = get_user(uaddr);
  if (result == -1) {
    error(f);
  }
  return result;
}

static void put_user_byte(uint8_t *udst, uint8_t byte, struct intr_frame *f) {
  if (is_kernel_vaddr(udst) || !put_user(udst, byte)) {
    error(f);
  }
}


/* Reads a byte at user virtual address UADDR.
UADDR must be below PHYS_BASE.
Returns the byte value if successful, -1 if a segfault
occurred. */
static int get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
  : "=&a" (result) : "m" (*uaddr));
  return result;
}
/* Writes BYTE to user address UDST.
UDST must be below PHYS_BASE.
Returns true if successful, false if a segfault occurred. */
static bool put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
  : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}


static struct fnode *get_fn_from_fd(int fd) {
  struct list *list_f = &thread_current()->file_list;
  struct list_elem *e = list_begin(list_f);
  for (; e != list_end(list_f); e = list_next(e)) {
    struct fnode *fn = list_entry(e, struct fnode, elem);
    if (fn->fd == fd)
      return fn;
  }
  return NULL;
}


static void error(struct intr_frame *f) {
  f->eax = -1;
  thread_current()->pn->exit_status = f->eax;
  printf ("%s: exit(%d)\n", thread_current()->name, f->eax);
  thread_exit();
}
