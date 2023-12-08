#include "types.h"
#include "user.h"

int main() {
  int dummy = 0;
  for (unsigned int i = 0; i < 1e9; i++) {  // lower `i < 4e9` to 1e9
    dummy += i;
  }

  exit();
}