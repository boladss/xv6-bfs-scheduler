/*
This simply reuses schedlog_test.c, 
but uses nicefork() to set nice values instead.
*/

#include "types.h"
#include "user.h"

int main() {
  schedlog(10000);

  for (int i = 0; i < 3; i++) {
    int nice_value = i;
    if (nicefork(nice_value) == 0) {
      char *argv[] = {"loop", 0};
      exec("loop", argv);
    }
  }

  for (int i = 0; i < 3; i++) {
    wait();
  }

  shutdown();
}