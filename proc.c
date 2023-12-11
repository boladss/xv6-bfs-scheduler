#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "bfs.h"

struct node {
  struct proc *proc;
  struct node *prev;
  struct node *next;
  struct node *lower;
};

struct ptable {
  struct spinlock lock;
  struct node level[LEVELS][NPROC + 1];
  struct proc proc[NPROC];
} ptable;

void delete(struct ptable *ptable, struct proc *proc) {
  //cprintf("DELETING PID %d: %s\n", proc->pid, proc->name);
  const int curr_deadline = proc->virt_deadline;

  //need to perform the proper skiplist search
  //start with highest head node
  int level = LEVELS - 1;
  struct node * curr = ptable->level[level];

  while (curr->proc != proc) {
    if (curr->next != 0 && curr_deadline >= curr->next->proc->virt_deadline)
      curr = curr->next; //go forward until next deadline is bigger than current deadline
    else if (curr->lower != 0)
      curr = curr->lower; //go down if can no longer go forward
    else if (curr->proc == 0) { //still at head node, go down to a lower level
      level--;
      curr = ptable->level[level];
    }
    else //can't go forward or down
      panic("node set for deletion not found\n"); //shouldn't be in this block if the node can be found
  }

  //delete the node and all nodes below it
  do {
    //remove all references
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
}

static unsigned int seed = 6969420;
unsigned int random(uint max) {
  seed ^= seed << 17;
  seed ^= seed >> 7;
  seed ^= seed << 5;
  return seed % max;
}

void insert(struct ptable *ptable, struct proc *proc) {
  //cprintf("INSERTING PID %d: %s\n", proc->pid, proc->name);
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
    if (level != 0 && random(10000) >= 2500) { // check level first to short the AND check
      return;
    }

    // find a place in the ptable array to store the struct. just use the first unallocated index
    struct node *node = ptable->level[level];
    node++; // index 0 is the head node, so just skip it
    while (node->proc != 0) {
      node++;
      if (node >= &ptable->level[level][NPROC + 1])
        panic("not enough memory to store a new value\n");
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


int
nicefork(int nice_value)
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

  np->nice = nice_value;   // VERIFY: Should this be added earlier?

  // computes virtual deadline based on niceness and quantum
  np->virt_deadline = ticks + (nice_value * BFS_DEFAULT_QUANTUM); 

  acquire(&ptable.lock);

  np->state = RUNNABLE;
  insert(&ptable, np); //needed to push new processes into skiplist

  release(&ptable.lock);

  return pid;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
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
  delete(&ptable, curproc); //from running/runnable to zombie, can't be in the skip list
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

// Add these declarations anywhere before the scheduler function
int schedlog_active = 0;
int schedlog_lasttick = 0;

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
  cprintf("entering scheduler\n");
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
      // cprintf((p == ptable.level[0][0].next->proc) ? "TRUE\n" : "FALSE\n");
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;
      p->ticks_left = BFS_DEFAULT_QUANTUM; // ticks_left implementation
      // p->ticks_done = 0;                    // ticks_done implementation

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

          // TEMPORARY VALUES:
          int temp_maxlevel = 4;
          // int temp_quantum = BFS_DEFAULT_QUANTUM;

          /*
          CLARIFICATIONS:
          Based on the example in the Project 1 specs, there's a few ambiguous
          differences from Lab 5:
          - Are unused processes not printed at all for the project? (as opposed
          to `[PID] ---:0`)
          - Should there be an indicator for the current process running?
          (marked with `*` in Lab 5)
          - Does <quantum> refer to ticks left or ticks done?
          - Other: maxlevel is not present in the example processes
          */

          for (int k = 0; k <= highest_idx; k++) {
            pp = &ptable.proc[k];
            if (pp->state == UNUSED) {
            }
            // cprintf("[%d]---:0,", k);
            else if (pp->state == RUNNING)
              cprintf("[%d]*%s:%d:%d(%d)(%d)(%d),", k, pp->name, pp->state,
                      pp->nice, temp_maxlevel, pp->virt_deadline, pp->ticks_left);
            else
              cprintf("[%d] %s:%d:%d(%d)(%d)(%d),", k, pp->name, pp->state,
                      pp->nice, temp_maxlevel, pp->virt_deadline, pp->ticks_left);
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
  //cprintf("switching from %s to scheduler\n", p->name);
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE; 
  //do not include an insert/delete here
  //note that yield sets a running process to runnable
  //both of which are states allowed to be in the skip list
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
  delete(&ptable, p); //from running/runnable to sleeping, can't be in the skip list

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
      insert(&ptable, p); //from sleeping to runnable? have to insert that
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
