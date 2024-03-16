// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define  HBSIZE 13

struct {
  struct buf buf[NBUF];

  struct spinlock hblock[HBSIZE];
  
  // restore free block
  struct buf head;
  struct spinlock lock; // lock head
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  char name[16];
  for (int i = 0; i < HBSIZE; i++) {
    snprintf(name, 15, "bcache_buc%i", i);
    bcache.buf[i].prev = &bcache.buf[i];
    bcache.buf[i].next = &bcache.buf[i];
    initlock(&bcache.hblock[i], name);
  }

  // Create linked list of buffers
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  
  for(b = bcache.buf + HBSIZE; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}

uint hash(uint a, uint b) {
  return (((uint64)a << 32) | b) % HBSIZE;
}

// acquire lock before call this function
// pick a free buf from buffer cache
// if unable to pick, return head
struct buf*
pick() {
  struct buf* b = bcache.head.next;
  if (b == &bcache.head) {
    return b;
  }
  b->next->prev = b->prev;
  b->prev->next = b->next;
  return b;
}

int
is_hashhead(struct buf *b) {
  return b < bcache.buf + HBSIZE;
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int bucket_idx = hash(dev, blockno);
  struct buf *hash_head = &bcache.buf[bucket_idx];
  acquire(&bcache.hblock[bucket_idx]);

  // Is the block already cached?
  if (hash_head->dev == dev && hash_head->blockno == blockno) {
    b = hash_head;
    if (b->refcnt == 0) {
      b->valid = 0;
    }
    b->refcnt++;
    release(&bcache.hblock[bucket_idx]);
    acquiresleep(&b->lock);
    return b;
  }

  for(b = hash_head->next; b != hash_head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.hblock[bucket_idx]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.

  acquire(&bcache.lock);
  b = pick();
  release(&bcache.lock);
  if (b == &bcache.head) {
    panic("bget: no buffers");
  }
  b->dev = dev;
  b->blockno = blockno;
  b->valid = 0;
  b->refcnt = 1;

  b->next = hash_head->next;
  b->prev = hash_head;
  hash_head->next->prev = b;
  hash_head->next = b;

  release(&bcache.hblock[bucket_idx]);
  acquiresleep(&b->lock);
  return b;
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");
  int bucket_idx = hash(b->dev, b->blockno);
  releasesleep(&b->lock);
  acquire(&bcache.hblock[bucket_idx]);
  b->refcnt -= 1;
  if (b->refcnt > 0 || is_hashhead(b)) {
    release(&bcache.hblock[bucket_idx]);
    return;
  }
  b->prev->next = b->next;
  b->next->prev = b->prev;
  release(&bcache.hblock[bucket_idx]);

  acquire(&bcache.lock);
  b->next = bcache.head.next;
  b->prev = &bcache.head;
  bcache.head.next->prev = b;
  bcache.head.next = b;
  release(&bcache.lock);
}

void
bpin(struct buf *b) {
  int bucket_idx = hash(b->dev, b->blockno);

  acquire(&bcache.hblock[bucket_idx]);
  b->refcnt++;
  release(&bcache.hblock[bucket_idx]);
}

void
bunpin(struct buf *b) {
  int bucket_idx = hash(b->dev, b->blockno);

  acquire(&bcache.hblock[bucket_idx]);
  b->refcnt--;
  release(&bcache.hblock[bucket_idx]);
}
