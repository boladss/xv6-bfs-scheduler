#include "types.h"
#include "user.h"

#define LEVELS 4
#define MPROC 6

struct proc {
  int pid;           // Process ID
  int nice;          // nice value
  int virt_deadline; // nice value
};

struct node {
  struct proc *proc;
  struct node *prev;
  struct node *next;
  struct node *lower;
};

struct ptable {
  struct node level[LEVELS][NPROC + 1];
  struct proc proc[NPROC];
} ptable;

static uint seed = 6969420;
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

int main() {
  for (int i = 0; i < LEVELS; i++) {
    for (int j = 0; j < NPROC + 1; j++) {
      ptable.level[i][j].next = 0;
      ptable.level[i][j].prev = 0;
      ptable.level[i][j].lower = 0;
      ptable.level[i][j].proc = 0;
    }
  }

  // INSERT TESTING
  // add a new process
  ptable.proc[0].pid = 1;
  ptable.proc[0].virt_deadline = 12;
  printf(1, "out: insert 0: %p, %p\n", &ptable, &ptable.proc[0]);
  insert(&ptable, &ptable.proc[0]);
  printf(1, "Addr of proc 0: %p\n", &ptable.proc[0]);
  for (int i = 0; i < LEVELS; i++) {
    for (int j = 0; j < 2; j++) {
      printf(1,
             "Level %d-%d | addr: %p, ->proc: %p; ->next: %p; ->prev: %p, "
             "->lower: %p\n",
             i, j, &ptable.level[i][j], ptable.level[i][j].proc,
             ptable.level[i][j].next, ptable.level[i][j].prev,
             ptable.level[i][j].lower);
    }
  }
  ptable.proc[1].pid = 2;
  ptable.proc[1].virt_deadline = 6;
  printf(1, "out: insert 1; %p, %p\n", &ptable, &ptable.proc[1]);
  insert(&ptable, &ptable.proc[1]);
  printf(1, "Addr of proc 1: %p\n", &ptable.proc[1]);
  for (int i = 0; i < LEVELS; i++) {
    for (int j = 0; j < 3; j++) {
      printf(1,
             "Level %d-%d | addr: %p, ->proc: %p; ->next: %p; ->prev: %p, "
             "->lower: %p\n",
             i, j, &ptable.level[i][j], ptable.level[i][j].proc,
             ptable.level[i][j].next, ptable.level[i][j].prev,
             ptable.level[i][j].lower);
    }
  }

  /*
  // GET TESTING
  ptable.proc[1].pid = 2;
  printf(1, 1, "Addr of proc: %p\n", &ptable.proc[1]);
  ptable.level[0][1].lower = &ptable.proc[1];
  ptable.level[1][1].lower = &ptable.level[0][1];
  ptable.level[2][1].lower = &ptable.level[1][1];
  ptable.level[3][1].lower = &ptable.level[2][1];
  printf(1, 1, "Addr from get level 0: %p\n", get(0, &ptable.level[0][1]));
  printf(1, 1, "Addr from get level 1: %p\n", get(1, &ptable.level[1][1]));
  printf(1, 1, "Addr from get level 2: %p\n", get(2, &ptable.level[2][1]));
  printf(1, 1, "Addr from get level 3: %p\n", get(3, &ptable.level[3][1]));
  printf(1, 1, "-------------END GET TESTING---------------\n\n");
  */
  exit();
};