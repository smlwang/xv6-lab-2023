// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);
#define PA2IDX(pa) (((uint64)(pa) - KERNBASE) / PGSIZE)
struct {
  struct spinlock lock;
  int count[(PHYSTOP - KERNBASE) / PGSIZE + 1];
} pgref;

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.


void acquire_ref() {
  acquire(&pgref.lock);
}
void release_ref() {
  release(&pgref.lock);
}

void sub_count(void *pa) {
  pgref.count[PA2IDX(pa)] -= 1;
}

void add_count(void *pa) {
  pgref.count[PA2IDX(pa)] += 1;
}

int get_count(void *pa) {
  return pgref.count[PA2IDX(pa)];
}
void set_count(void *pa, int count) {
  pgref.count[PA2IDX(pa)] = count;
}

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&pgref.lock, "pgref");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    set_count(p, 1);
    kfree(p);
  }
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire_ref();
  sub_count(pa);
  if (get_count(pa) > 0) {
    release_ref();
    return;
  }
  release_ref();

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r){
    memset((char*)r, 5, PGSIZE); // fill with junk
    acquire_ref();
    add_count(r);
    release_ref();
  }
  return (void*)r;
}
