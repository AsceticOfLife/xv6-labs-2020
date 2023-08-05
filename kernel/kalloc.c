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

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

// struct {
//   struct spinlock lock;
//   struct run *freelist;
// } kmem;
struct Kmem {
  struct spinlock lock;
  struct run *freelist;
};
struct Kmem kmem[NCPU];

void
kinit()
{
  for (int i = 0; i < NCPU; ++i) {
    push_off(); // shutdown interrupt
    int id = cpuid();
    if (id != i) {
      pop_off();
      continue;
    }

    initlock(&kmem[i].lock, "kmem");
    pop_off();
    freerange(end, (void*)PHYSTOP);
  } 
  // initlock(&kmem.lock, "kmem");
  // freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  // get current cpuid
  push_off();
  int id = cpuid();
  pop_off();

  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off();
  int id = cpuid();
  pop_off();

  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if(r)
    kmem[id].freelist = r->next;
  release(&kmem[id].lock);

  // steal one page from other cpu
  int temp_id = id + 1;
  while (r == 0) {
    if (temp_id >= NCPU) temp_id = 0;
    if (temp_id == id) break; // 循环一遍其它的cpu均没有可用的空闲内存

    acquire(&kmem[temp_id].lock);
    r = kmem[temp_id].freelist;
    if (r) {
      // 如果偷到一块内存
      kmem[temp_id].freelist = r->next;
      release(&kmem[temp_id].lock);
      break;
    } else {
      // 释放temp_id的锁，继续寻找其它cpu的空闲内存
      release(&kmem[temp_id].lock);
      temp_id++;
    }
  }

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
