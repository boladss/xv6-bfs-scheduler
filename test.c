#include "types.h"
#include "user.h"

#define LEVELS 4
#define MPROC 4

struct proc {
  int pid;                     // Process ID
  int nice;                    // nice value
  int virt_deadline;           // nice value
};

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
  struct node level[LEVELS][NPROC + 1];
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
    else //bottom of the list, can't go forward or down
      printf(1, "node set for deletion not found\n"); //shouldn't be in this block if the node can be found
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


static uint seed = 6969420;
unsigned int random(uint max) {
  seed ^= seed << 17;
  seed ^= seed >> 7;
  seed ^= seed << 5;
  return seed % max;
}
void insert(struct ptable *ptable, struct proc * proc) {
  //idea: iterate through the bottommost level and place it where appropriate there
  //go up through the levels one-by-one and get rand; if passes random, then do the same for that level
  struct node * lower = 0; //see struct node for void reasoning
  uint level = 0;
  struct proc * p = proc; //need to type narrow proc to get the virtual deadline
  const int curr_deadline = p->virt_deadline;

  while (level < LEVELS) {
    //roll the dice; level 0 is guaranteed
    if (level != 0 && random(10000) >= 2500) { //check level first to short the AND check
      break; //failed the cointoss? no point in going higher
    }
    //first, find a place in the array to store the struct. just use the first unallocated index
    struct node * node = ptable->level[level];
    do {
      node++; //index 0 is the head node, so just skip it

      if (node >= &ptable->level[level][NPROC + 1])
        printf(1, "not enough memory to store a new value\n");
    } while (node->proc != 0);
    node->lower = lower;
    node->proc = proc;

    //iterate through the current level to find where to insert
    struct node * curr = ptable->level[level];
    while (curr->next != 0 && curr_deadline < curr->proc->virt_deadline) {
      curr++;
    }

    //insert node
    node->prev = curr;
    node->next = curr->next;
    curr->next->prev = node;
    curr->next = node;

    //prep for next iteration
    lower = node; //void can be set to node
    level++;
  }
}

int main() {
  printf(1, "%d\n", sizeof(int));
  for(int i = 0; i < LEVELS; i++) {
    for(int j = 0; j < NPROC + 1; j++) {
      ptable.level[i][j].next = 0;
      ptable.level[i][j].prev = 0;
      ptable.level[i][j].lower = 0;
      ptable.level[i][j].proc = 0;
    }
  }

  //INSERT TESTING
  //add a new process
  ptable.proc[0].pid = 1;
  ptable.proc[0].virt_deadline = 12;
  insert(&ptable, &ptable.proc[0]);
  printf(1, "Addr of proc 0: %p\n", &ptable.proc[0]);
  for (int i = 0; i < LEVELS; i++) {
    for (int j = 0; j < 2; j++) {
      printf(1, "Level %d-%d | addr: %p, ->proc: %p; ->next: %p; ->prev: %p, ->lower: %p\n", i, j, &ptable.level[i][j], ptable.level[i][j].proc, ptable.level[i][j].next, ptable.level[i][j].prev, ptable.level[i][j].lower);
    }
  }
  ptable.proc[1].pid = 2;
  ptable.proc[1].virt_deadline = 6;
  insert(&ptable, &ptable.proc[1]);
  printf(1, "Addr of proc 1: %p\n", &ptable.proc[1]);
  for (int i = 0; i < LEVELS; i++) {
    for (int j = 0; j < 3; j++) {
      printf(1, "Level %d-%d | addr: %p, ->proc: %p; ->next: %p; ->prev: %p, ->lower: %p\n", i, j, &ptable.level[i][j], ptable.level[i][j].proc, ptable.level[i][j].next, ptable.level[i][j].prev, ptable.level[i][j].lower);
    }
  }


/*   //GET TESTING
  ptable.proc[1].pid = 2;
  printf(1, "Addr of proc: %p\n", &ptable.proc[1]);
  ptable.level[0][1].lower = &ptable.proc[1];
  ptable.level[1][1].lower = &ptable.level[0][1];
  ptable.level[2][1].lower = &ptable.level[1][1];
  ptable.level[3][1].lower = &ptable.level[2][1];
  printf(1, "Addr from get level 0: %p\n", get(0, &ptable.level[0][1]));
  printf(1, "Addr from get level 1: %p\n", get(1, &ptable.level[1][1]));
  printf(1, "Addr from get level 2: %p\n", get(2, &ptable.level[2][1]));
  printf(1, "Addr from get level 3: %p\n", get(3, &ptable.level[3][1]));
  printf(1, "-------------END GET TESTING---------------\n\n"); */

  exit();
}