#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;


  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}
// #define LAB_PGTBL
#ifdef LAB_PGTBL
int
sys_pgaccess(void)
{
  uint64 base; // address to access
  int len;
  uint64 mask; // address of return value;

  argaddr(0, &base);
  argint(1, &len);
  argaddr(2, &mask);

  if (len > 32) {
    len = 32;
  }
  pagetable_t pagetable = myproc()->pagetable;
  // printf("base: %p\n", base);
  // vmprint(pagetable);
  pte_t *pte;
  int access_mask = 0;
  for (int page = 0; page < len; page++) {
    if ((pte = walk(pagetable, base, 0)) == 0) {
      return -1;
    }
    base += PGSIZE;
    if (*pte & PTE_A) {
      access_mask |= 1L << page;
      *pte ^= PTE_A; // reset PTE_A flag
    }
  }
  // printf("%p\n", access_mask);
  if (copyout(pagetable, mask, (char *)&access_mask, sizeof(access_mask)) < 0) {
    return -1;
  }
  return 0;
}
#endif

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
