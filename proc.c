#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

/*struct node pop(skiplist *slist) {
  struct node *next = slist->level[0].head.next;
  return *next;
} 

void update(skiplist ) {

}

struct node * searchpid(skiplist *slist, int pid) {
  //this is exhaustive search
  //because the skiplist isn't sorted by pid
  //it's sorted by virtual deadline
  //so you have to exhaustive search anyway

  //iterate through the levels from top to bottom
  for(int i = slist->levels - 1; i >= 0; i--) {
    if (slist->level[i].length < 1) {
      continue; //current list is empty, so keep goin
    }
    struct node *curr = slist->level[i].head;
    while (curr->next != 0) { //iterate through the list
      if (curr->proc->pid == pid) {
        return curr; //return if found
      }
    }
  }
  return 0; //otherwise, return nothing
};
 
struct proc * get(int level, union ptr * node) {
  //higher levels are only pointers,
  //need to go down to the bottom level to access actual content
  union ptr * curr = node; //void * can be type narrowed to node *
  
  while (level > 0) {
    curr = curr->lower; //void * can be type narrowed to node *
    level--;
  }
  struct proc * p = curr->lower; //void * can be type narrowed to proc *

  //should explicitly type-check that p is a pointer to a proc
  //but C doesn't store type information and structs are out of the question due to the lack of malloc
  //there isn't a try-catch block in kernel mode either
  //up to programmer to guarantee this returns the correct pointer
  return p;

  struct proc * get(int level, void * node) {
  //higher levels are only pointers,
  //need to go down to the bottom level to access actual content
  struct node * curr = node; //void * can be type narrowed to node *
  
  while (level > 0) {
    curr = curr->lower; //void * can be type narrowed to node *
    level--;
  }
  struct proc * p = curr->lower; //void * can be type narrowed to proc *

  //should explicitly type-check that p is a pointer to a proc
  //but C doesn't store type information and structs are out of the question due to the lack of malloc
  //there isn't a try-catch block in kernel mode either
  //up to programmer to guarantee this returns the correct pointer
  return p;
}

}
 */

struct node {
  struct proc *proc;
  struct node *prev;
  struct node *next;
  struct node *lower;
  //lower is either struct node ***, struct node **, struct node *, or struct proc *
  //which increases in asterisks the more levels there are.
  //easier to just set it to void than create a union
};

struct ptable {
  struct spinlock lock;
  struct node level[LEVELS][NPROC + 1]; //an array of either an array of processes (level 0) or an array of pointers (level 1+)
  struct proc proc[NPROC];
} ptable;

void delete(struct ptable *ptable, struct proc *proc) {
  const int curr_deadline = proc->virt_deadline;

  //need to perform the proper skiplist search
  //start with highest head node
  struct node * curr = ptable->level[LEVELS - 1];

  while (curr->proc != proc) {
    if (curr->next != 0 && curr_deadline <= curr->next->proc->virt_deadline)
      curr = curr->next; //go forward until next deadline is bigger than current deadline
    else if (curr->lower != 0)
      curr = curr->lower; //go down if can no longer go forward
    else //can't go forward or down
      panic("node set for deletion not found\n"); //shouldn't be in this block if the node can be found
  }

  //delete the node and all nodes below it
  do {
    //remove all references
    curr->prev->next = curr->next;
    curr->next->prev = curr->prev;

    //deallocate current node
    curr->proc = 0; //technically only this has to be set... but might as well do the rest
    curr->prev = 0;
    curr->next = 0;

    //iterate to node below current node
    struct node * temp = curr; //need to store the current node to deallocate ->lower
    curr = curr->lower; //set curr to lower
    temp->lower = 0; //then deallocate
  } while (curr != 0);
}

struct proc * dequeue(struct ptable *ptable) {
  //get the first node in the bottommost level
  struct node * node = ptable->level[0][0].next;
  struct proc * popped = node->proc;

  //then delete the first node
  delete(ptable, popped);

  return popped;
}

static unsigned int seed = 6969420;
unsigned int random(uint max) {
  seed ^= seed << 17;
  seed ^= seed >> 7;
  seed ^= seed << 5;
  return seed % max;
}

void insert(struct ptable *ptable, struct proc *proc) {
  const int curr_deadline = proc->virt_deadline;
  struct node *placement[LEVELS];
  int level = LEVELS - 1;

  // first: find all nodes which mark the locations of where to place new node
  struct node *curr = &ptable->level[level][0];
  while (level >= 0) {
    if (curr->next != 0 && curr->next->proc->virt_deadline < curr_deadline)
      curr = curr->next; // go forward until next deadline is bigger than or equal to current deadline
    else if (curr->proc == 0) { // still at the head node
      placement[level] = curr;
      level--;
      curr = &ptable->level[level][0];
    } else {                   // can no longer go forward
      placement[level] = curr; // first, push node to placements array
      level--;                 // reduce level
      if (curr->lower != 0)    // if can go lower, go lower
        curr = curr->lower;
    }
  }
  // next, iterate through all the levels and insert
  struct node *lower = 0;
  for (level = 0; level < LEVELS; level++) {
    // roll the dice; level 0 is guaranteed
    int rand = random(10000);
    printf(1, "LEVEL: %d, RAND: %d\n", level, rand);
    if (level != 0) { // check level first to short the AND check
      if (rand >= 2500)
        return;
    }

    // find a place in the ptable array to store the struct. just use the first unallocated index
    struct node *node = ptable->level[level];
    node++; // index 0 is the head node, so just skip it
    while (node->proc != 0) {
      node++;

      if (node >= &ptable->level[level][NPROC + 1])
        printf(1, "not enough memory to store a new value\n");
    }
    node->lower = lower;
    node->proc = proc;

    // perform insert
    curr = placement[level];
    node->prev = curr;
    node->next = curr->next;
    curr->next = node;
    if (node->next != 0) { // if not at the end yet
      node->next->prev = node;
    }

    // prep for next iteration
    lower = node;
  }
}

/*static skiplist slist = {
  4, 
  {
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0}
  }
}; */

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
  acquire(&ptable.lock);
  //this functions as a ghetto allocation for, at most, NPROC nodes in the linked list
  for(int i = 0; i < LEVELS; i++) {
    for(int j = 0; j < NPROC + 1; j++) {
      ptable.level[i][j].next = 0;
      ptable.level[i][j].prev = 0;
      ptable.level[i][j].lower = 0;
    }
  }
  release(&ptable.lock);
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->virt_deadline = 0; //change this later
  p->nice = 0; //nice defaults to 0
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}
