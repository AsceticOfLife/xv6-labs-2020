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

#define NBUCKETS 97 // number of buckets
#define NSIZE 2 // size of bucket

struct {
  struct spinlock lock;
  char name[10]; // name of buckets
  struct buf buf[NSIZE];

  // // Linked list of all buffers, through prev/next.
  // // Sorted by how recently the buffer was used.
  // // head.next is most recent, head.prev is least.
  // struct buf head;
} bcache[NBUCKETS];

void
binit(void)
{
  struct buf *b;

  for (int i = 0; i < NBUCKETS; i++) {
    // 对每个桶进行初始化
    snprintf(bcache[i].name, 10, "bcache%d", i);
    initlock(&bcache[i].lock, bcache[i].name);

    // 初始化每个桶中的buf
    for (int j = 0; j < NSIZE; j++) {
      b = &bcache[i].buf[j];
      initsleeplock(&b->lock, "buffer");
      b->refcnt = 0; // 引用次数为0
      b->timestamp = ticks;
    }
  }

}

// void
// binit(void)
// {
//   struct buf *b;

//   initlock(&bcache.lock, "bcache");

//   // Create linked list of buffers
//   bcache.head.prev = &bcache.head;
//   bcache.head.next = &bcache.head;
//   for(b = bcache.buf; b < bcache.buf+NBUF; b++){
//     b->next = bcache.head.next;
//     b->prev = &bcache.head;
//     initsleeplock(&b->lock, "buffer");
//     bcache.head.next->prev = b;
//     bcache.head.next = b;
//   }
// }

static struct buf*
bget(uint dev, uint blockno) {
  struct buf *b;

  // 根据块号计算出hash表中的序号
  int id = blockno % NBUCKETS;
  // 获取序号对应的桶的锁
  acquire(&bcache[id].lock);

  // 在桶中查找是否存在块
  for (int i = 0; i < NSIZE; i++) {
    b = &bcache[id].buf[i];
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++; // 引用次数加1
      release(&bcache[id].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 如果桶子中没有已经缓存的块
  // 那么就需要在桶子中找到一个时间戳最小的块
  // 时间戳最小说明最久没有使用过
  uint min = -1;
  struct buf *temp = 0;
  for (int i = 0; i < NSIZE; i++) {
    b = &bcache[id].buf[i];
    if (b->refcnt == 0 && b->timestamp < min) {
      min = b->timestamp;
      temp = b;
    }
  }

  // 如果找到了一个合适的buf
  if (min != -1) {
    temp->dev = dev;
    temp->blockno = blockno;
    temp->valid = 0;
    temp->refcnt = 1;
    release(&bcache[id].lock);
    acquiresleep(&temp->lock);
    return temp;
  }

  panic("bget: no buffers");
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
// static struct buf*
// bget(uint dev, uint blockno)
// {
//   struct buf *b;

//   acquire(&bcache.lock);

//   // Is the block already cached?
//   for(b = bcache.head.next; b != &bcache.head; b = b->next){
//     if(b->dev == dev && b->blockno == blockno){
//       b->refcnt++;
//       release(&bcache.lock);
//       acquiresleep(&b->lock);
//       return b;
//     }
//   }

//   // Not cached.
//   // Recycle the least recently used (LRU) unused buffer.
//   for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
//     if(b->refcnt == 0) {
//       b->dev = dev;
//       b->blockno = blockno;
//       b->valid = 0;
//       b->refcnt = 1;
//       release(&bcache.lock);
//       acquiresleep(&b->lock);
//       return b;
//     }
//   }
//   panic("bget: no buffers");
// }

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

void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  // 根据区块号获取这个buf对应的桶
  int id = b->blockno % NBUCKETS;
  acquire(&bcache[id].lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // 更新未被使用的时间戳
    b->timestamp = ticks;
  }
  release(&bcache[id].lock);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
// void
// brelse(struct buf *b)
// {
//   if(!holdingsleep(&b->lock))
//     panic("brelse");

//   releasesleep(&b->lock);

//   acquire(&bcache.lock);
//   b->refcnt--;
//   if (b->refcnt == 0) {
//     // no one is waiting for it.
//     b->next->prev = b->prev;
//     b->prev->next = b->next;
//     b->next = bcache.head.next;
//     b->prev = &bcache.head;
//     bcache.head.next->prev = b;
//     bcache.head.next = b;
//   }
  
//   release(&bcache.lock);
// }

void
bpin(struct buf *b) {
  int id = b->blockno % NBUCKETS;
  acquire(&bcache[id].lock);
  b->refcnt++;
  release(&bcache[id].lock);
}

void
bunpin(struct buf *b) {
  int id = b->blockno % NBUCKETS;
  acquire(&bcache[id].lock);
  b->refcnt--;
  release(&bcache[id].lock);
}


