//
// Support functions for system calls that involve file descriptors.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "proc.h"
#include "fcntl.h"

struct devsw devsw[NDEV];
struct {
  struct spinlock lock;
  struct file file[NFILE];
} ftable;

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

// Allocate a file structure.
struct file*
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}

// Increment ref count for file f.
struct file*
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void
fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){
    release(&ftable.lock);
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if(ff.type == FD_PIPE){
    pipeclose(ff.pipe, ff.writable);
  } else if(ff.type == FD_INODE || ff.type == FD_DEVICE){
    begin_op();
    iput(ff.ip);
    end_op();
  }
}

// Get metadata about file f.
// addr is a user virtual address, pointing to a struct stat.
int
filestat(struct file *f, uint64 addr)
{
  struct proc *p = myproc();
  struct stat st;
  
  if(f->type == FD_INODE || f->type == FD_DEVICE){
    ilock(f->ip);
    stati(f->ip, &st);
    iunlock(f->ip);
    if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
      return -1;
    return 0;
  }
  return -1;
}

// Read from file f.
// addr is a user virtual address.
int
fileread(struct file *f, uint64 addr, int n)
{
  int r = 0;

  if(f->readable == 0)
    return -1;

  if(f->type == FD_PIPE){
    r = piperead(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
      return -1;
    r = devsw[f->major].read(1, addr, n);
  } else if(f->type == FD_INODE){
    ilock(f->ip);
    if((r = readi(f->ip, 1, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
  } else {
    panic("fileread");
  }

  return r;
}

// Write to file f.
// addr is a user virtual address.
int
filewrite(struct file *f, uint64 addr, int n)
{
  int r, ret = 0;

  if(f->writable == 0)
    return -1;

  if(f->type == FD_PIPE){
    ret = pipewrite(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
      return -1;
    ret = devsw[f->major].write(1, addr, n);
  } else if(f->type == FD_INODE){
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();

      if(r != n1){
        // error from writei
        break;
      }
      i += r;
    }
    ret = (i == n ? n : -1);
  } else {
    panic("filewrite");
  }

  return ret;
}

int
readpage(struct file* f, uint64 pa, uint64 offset) {
  printf("readpage\n");
  uint64 da = PGROUNDDOWN(offset);
  ilock(f->ip);
  if (readi(f->ip, 0, pa, da, PGSIZE) < 0) {
    iunlock(f->ip);
    return -1;
  }
  iunlock(f->ip);
  return 0;
}

int
munmap(uint64 addr, size_t len) {
  
  struct proc* p = myproc();
  
  if (addr % PGSIZE != 0) {
    return -1;
  }

  struct vma *v = 0;
  struct vma *sv = p->vmas;
  for (int i = 0; i < NVMA; i++) {
    if (sv->addr <= addr && sv->addr + sv->len > addr) {
      v = sv;
      break;
    }
    sv++;
  }
  if (v == 0 || (v->addr != addr && v->addr + v->len != addr + len)) {
    return -1;
  }
  if (len % PGSIZE) {
    return -1;
  }

  uint64 st = addr, ed = addr + len;

  struct file* f = v->file;
  for (; addr < ed; addr += PGSIZE) {
    pte_t *pte = walk(p->pagetable, addr, 0);
    uint64 pa = walkaddr(p->pagetable, addr);
    if (v->flags == MAP_SHARED && (*pte & PTE_D)) {
      uint n = addr + PGSIZE > v->addr + v->len ? v->addr + v->len - addr : PGSIZE;
      begin_op();
      ilock(f->ip);
      int w = writei(f->ip, 0, pa, addr - v->addr + v->offset, n);
      iunlock(f->ip);
      end_op();
      if (w != n) {
        return -1;
      }
    }
    // user not access that not alloc physical memory yet
    if (*pte & PTE_V) {
      uvmunmap(p->pagetable, addr, 1, 1);
    }
  }
  if (v->addr == st) {
    if (v->addr + v->len == ed) {
      fileclose(f);
      v->addr = 0;
    } else { // v->addr + v->len < ed
      v->offset += ed - v->addr;
      v->len -= ed - v->addr;
      v->addr = ed;
    }
  } else {
    v->len -= ed - st;
    v->addr = ed;
  }
  return 0;
}