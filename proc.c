#include "types.h"
#include "defs.h"
#include "param.h" //includes bfs.h already
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

// STRUCTS
struct node {
  struct proc *proc;
  struct node *prev;
  struct node *next;
  struct node *lower;
};
// node is defined here as no other file uses it anyway
// so no point having it in the header

// GLOBALS
static unsigned int seed = SEED;    // Used in random()

int schedlog_active = 0;            // Used in schedlog printing
int schedlog_lasttick = 0;

int ins_del_flag = 1;               // To hide skiplist insertion/deletion messages if necessary

struct ptable {
  struct spinlock lock;
  struct node level[LEVELS][NPROC + 1];
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

// SKIP LIST FUNCTIONS
static int computevd(int nice_value, int curr_ticks) {
  return curr_ticks + ((nice_value - NICE_FIRST_LEVEL + 1) * BFS_DEFAULT_QUANTUM);
}

static int delete(struct ptable *ptable, struct proc *proc) {
  if (ins_del_flag) cprintf("removed|[%d]%d\n", proc->pid, proc->max_skiplist_level);
  const int curr_deadline = proc->virt_deadline;
  struct node *placement[LEVELS];
  int level = LEVELS - 1;

  // first: find all nodes which mark the locations of where to check new node
  struct node *curr = &ptable->level[level][0];
  while (level >= 0) {
    if (curr->next != 0 && curr->next->proc->virt_deadline < curr_deadline)
      curr = curr->next; // go forward while next deadline is less than current deadline
      //stop going forward even if next node has an equal virtual deadline
      //which prioritizes to-be inserted node
    else if (curr->proc == 0) { // still at the head node
      placement[level] = curr;
      level--;
      curr = &ptable->level[level][0];
    } else {                   // can no longer go forward
      placement[level] = curr; // first, push node to placements array
      level--;                 // reduce level
      curr = curr->lower;      // go lower
      if (curr == 0 && level >= 0)    
        panic("deletion: above level 0 with no lower");
        //somehow got to a zero pointer without making it to level 0, so panic
    }
  }

  //then: iterate through the placements top to bottom until it is found
  //this guarantees that deletion is done starting from the highest level of a node
  //even if multiple nodes have equal virtual deadlines but unequal max skiplist levels
  for (level = LEVELS - 1; curr->proc != proc; level--) {
    if (level < 0)
      panic("deletion search beyond lowest level");

    curr = placement[level];
    while (curr->next != 0 //next value exists
      && curr->next->proc->virt_deadline == curr_deadline //next deadline and current deadline are still equal 
      && curr->proc != proc //process isn't found yet
    ) {
      curr = curr->next;
    }
  }

  //delete the node and all nodes below it
  do {
    //remove all references
    if (curr->prev == 0) //checked to prevent page faulting
      panic("somehow deleting node with no set previous\n");
    curr->prev->next = curr->next;
    if (curr->next != 0)
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
  level = proc->max_skiplist_level;
  proc->max_skiplist_level = -1; // absent from skiplist now shows level = -1, as per updated specs
  return level;
}

unsigned int random(uint max) {
  seed ^= seed << 17;
  seed ^= seed >> 7;
  seed ^= seed << 5;
  return seed % max;
}

static int insert(struct ptable *ptable, struct proc *proc) {
  const int curr_deadline = proc->virt_deadline;
  struct node *placement[LEVELS];
  int level = LEVELS - 1;

  // first: find all nodes which mark the locations of where to place new node
  struct node *curr = &ptable->level[level][0];
  while (level >= 0) {
    if (curr->next != 0 && curr->next->proc->virt_deadline < curr_deadline)
      curr = curr->next; // go forward while next deadline is less than current deadline
    else if (curr->proc == 0) { // still at the head node
      placement[level] = curr;
      level--;
      curr = &ptable->level[level][0];
    } else {                   // can no longer go forward
      placement[level] = curr; // first, push node to placements array
      level--;                 // reduce level
      curr = curr->lower;      // go lower
      if (curr == 0 && level >= 0)    
        panic("insertion: above level 0 with no lower");
        //somehow got to a zero pointer without making it to level 0, so panic
    }
  }
  // next, iterate through all the levels and insert
  struct node *lower = 0;
  for (level = 0; level < LEVELS; level++) {
    // roll the dice; level 0 is guaranteed
    if (level != 0 && random(10000) >= SKIPLIST_P) { // check level first to short the AND check
      break;
    }

    // find a place in the ptable array to store the struct. just use the first unallocated index
    struct node *node = ptable->level[level];
    node++; // index 0 is the head node, so just skip it
    while (node->proc != 0) {
      node++;
      if (node >= &ptable->level[level][NPROC + 1])
        panic("not enough memory to store a new value\n");
        //note that inserts only occur after a value has been placed into the pcb array
        //being in this block means a process fits in the pcb array but somehow doesn't fit in the skiplist
        //which shouldn't happen, so panic
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
  proc->max_skiplist_level = level; //escaped the for loop? congrats, that's the max level
  if (ins_del_flag) cprintf("inserted|[%d]%d\n", proc->pid, level);
  return level;
}

// xv6 FUNCTIONS
void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
  acquire(&ptable.lock);
  for (int i = 0; i < LEVELS; i++) {
    //initialize head node
    ptable.level[i][0].prev = 0;
    ptable.level[i][0].next = 0;
    ptable.level[i][0].lower = 0;
    for (int j = 0; j < NPROC + 1; j++) {
      //initialize nodes to be "unallocated" and usable by the insert function
      ptable.level[i][j].proc = 0;
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
  p->virt_deadline = 0; //default, should be changed by caller after
  p->nice = 0; //default, should be changed by caller after
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
  //set init to have the default nice value of 0
  p->nice = 0;
  p->virt_deadline = computevd(0, ticks);

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;
  insert(&ptable, p); //needed to push pid 0: init into skiplist

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
// Sets nice_value of process given input.
int
nicefork(int nice_value)
{
  // Check if nice value is valid
  if (nice_value < NICE_FIRST_LEVEL || nice_value > NICE_LAST_LEVEL) {
    panic("invalid nice value");
  }

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
    //no point deleting from the skiplist, nothing was ever added
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

  // sets nice value of process
  np->nice = nice_value;

  // computes virtual deadline based on niceness and quantum
  np->virt_deadline = computevd(nice_value, ticks); 
  
  acquire(&ptable.lock);

  np->state = RUNNABLE;
  insert(&ptable, np); //needed to push new processes into skiplist

  release(&ptable.lock);
  
  return pid;
}

// Preserve existing functionality of fork(), uses default nice value = 0.
int
fork(void)
{
  return nicefork(0);
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
  if (curproc->state == RUNNING || curproc->state == RUNNABLE)
    delete(&ptable, curproc); //from running/runnable to zombie, can't be in the skip list
                                      // note that delete returns the level PRIOR to deletion, actual current level is now -1

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
  release(&ptable.lock);
  acquire(&ptable.lock);
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
        //processes are removed from the skiplist upon zombification
        //so no point deleting them here
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


void schedlog(int n) {
  schedlog_active = 1;
  schedlog_lasttick = ticks + n;
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
  for (;;) {
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    if (ptable.level[0][0].next != 0) { //currently empty skiplist, so wait for something to run
      p = ptable.level[0][0].next->proc;
      if (p == 0)                       //proctable doesn't exist
        panic("selected process is 0\n"); 

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;
      p->ticks_left = BFS_DEFAULT_QUANTUM; // ticks_left implementation

      // Print schedlog details
      if (schedlog_active) {
        if (ticks > schedlog_lasttick) {
          schedlog_active = 0;
        } else {
          cprintf("%d|", ticks);

          struct proc *pp;
          int highest_idx = -1;

          for (int k = 0; k < NPROC; k++) {
            pp = &ptable.proc[k];
            if (pp->state != UNUSED) {
              highest_idx = k;
            }
          }


          for (int k = 0; k <= highest_idx; k++) {
            pp = &ptable.proc[k];
            if (pp->state == UNUSED) cprintf("[-]---:0:-(-)(-)(-)");
            else {
              // [<pid>]<name>:<state>:<nice_level>(<max_level>)(<virtual_deadline>)(<quantum_left>)
              cprintf("[%d]%s:%d:%d(%d)(%d)(%d)",
                pp->pid, pp->name, pp->state, pp->nice,
                pp->max_skiplist_level, pp->virt_deadline, pp->ticks_left);
            }
            if (k != highest_idx) cprintf(",");
          }
          cprintf("\n");
        }
      }

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
  struct proc * p = myproc();
  acquire(&ptable.lock);  //DOC: yieldlock
  p->state = RUNNABLE; 

  //need to recompute virt deadline
  delete(&ptable, p);
  p->virt_deadline = computevd(p->nice, ticks);
  insert(&ptable, p);
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
  if (p->state == RUNNABLE || p->state == RUNNING)
    delete(&ptable, p); //from running/runnable to sleeping, can't be in the skip list
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
    if(p->state == SLEEPING && p->chan == chan) {
      p->state = RUNNABLE;
      insert(&ptable, p);
    }
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
      if(p->state == SLEEPING) {
        p->state = RUNNABLE;
        insert(&ptable, p); //from sleeping to runnable? have to insert that
      }
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
