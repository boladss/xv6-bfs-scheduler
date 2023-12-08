#include "proc.h"

struct node{
  struct proc *proc; //points to the proc datastructure of a node
  struct node *next; //points to the next node in the same level
  struct node *lower; //points to the same node in a lower level
  struct node *prev; //points to previous node in the same level; helps with deletion
};

typedef struct { 
  int length;
  struct node *head;
} linkedlist; 

typedef struct {
  //skiplist can only have 4 levels 
  //so just initialize all 4 levels
  uint levels;
  linkedlist level[4];
} skiplist;