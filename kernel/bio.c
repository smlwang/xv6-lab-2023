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
  uint control;
  struct spinlock lock; // lock buf
  
  struct buf hhead[HBSIZE];
  struct spinlock hblock[HBSIZE];
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");
  bcache.control = 0x3fffffff;
  char name[16];
  for (int i = 0; i < HBSIZE; i++) {
    int ed = snprintf(name, 15, "bcache_%i", i);
    name[ed] = 0;
    bcache.hhead[i].prev = &bcache.hhead[i];
    bcache.hhead[i].next = &bcache.hhead[i];

    initlock(&bcache.hblock[i], name);
  }
  
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    initsleeplock(&b->lock, "buffer");
  }
}

uint hash(uint a, uint b) {
  return (((uint64)a << 32) | b) % HBSIZE;
}
static const int bit_map[16] = {-1, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0};

// need acquire lock before call this function
// pick a free buf from buffer cache
// if unable to pick, return -1
int
pick(struct buf** b) {
  uint control = bcache.control;
  int idx = 0;
  do {
    if (bit_map[control & 0xf] == -1) {
      idx += 4;
    } else {
      idx += bit_map[control & 0xf];
      bcache.control ^= 1 << idx;
      *b = bcache.buf + idx;
      return 0;
    }
  } while (control >>= 4);

  return -1;
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int bucket_idx = hash(dev, blockno);
  struct buf *hash_head = &bcache.hhead[bucket_idx];
  acquire(&bcache.hblock[bucket_idx]);

  // Is the block already cached?

  for(b = hash_head->next; b != hash_head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.hblock[bucket_idx]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  // Not cached.
  // pick new buffer.
  acquire(&bcache.lock);
  if (pick(&b) < 0) {
    panic("bget: no buffers");
  }
  release(&bcache.lock);
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
  if (b->refcnt > 0) {
    release(&bcache.hblock[bucket_idx]);
    return;
  }
  bcache.control ^= 1 << (b - bcache.buf);
  b->prev->next = b->next;
  b->next->prev = b->prev;
  release(&bcache.hblock[bucket_idx]);
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
