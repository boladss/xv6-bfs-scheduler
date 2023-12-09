#include "proc.h"
#include "param.h"

struct skiplist {
  uint levels;
  union {
    void * p[NPROC];
    struct proc proc[NPROC];
  } level[4]; //an array of either an array of processes (level 0) or an array of pointers (level 1+)
};