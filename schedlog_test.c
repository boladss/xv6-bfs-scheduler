#include "types.h"
#include "user.h"

int main() {
  schedlog(10000);

  for (int i = 0; i < 3; i++) {
    if (fork() == 0) {
      char *argv[] = {"schedlog_loop", 0};
      exec("schedlog_loop", argv);
    }
  }

  for (int i = 0; i < 3; i++) {
    wait();
  }

  shutdown();
}