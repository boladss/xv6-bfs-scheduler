#include "proc.h"
#include "param.h"

struct skiplist {
  uint levels;
  struct proc ** level[4][NPROC]; //an array of either an array of processes (level 0) or an array of pointers (level 1+)
  struct proc proc[NPROC];
};