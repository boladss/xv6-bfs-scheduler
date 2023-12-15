// Test that fork fails gracefully.
// Tiny executable so that the limit can be filling the proc table.

#include "types.h"
#include "stat.h"
#include "user.h"

#define N  1000

void
niceforktest(void)
{
  int n, pid;

  printf(1, "nicefork test\n");

  for(n=0; n<N; n++){


    int nice = n % (NICE_LAST_LEVEL - NICE_FIRST_LEVEL + 1) - NICE_LAST_LEVEL - 1;

    pid = nicefork(nice);
    if(pid < 0)
      break;
    if(pid == 0)
      exit();
  }

  if(n == N){
    printf(1, "nicefork claimed to work N times!\n", N);
    exit();
  }

  for(; n > 0; n--){
    if(wait() < 0){
      printf(1, "wait stopped early\n");
      exit();
    }
  }

  if(wait() != -1){
    printf(1, "wait got too many\n");
    exit();
  }

  printf(1, "nicefork test OK\n");
}

int
main(void)
{
  niceforktest();
  exit();
}
