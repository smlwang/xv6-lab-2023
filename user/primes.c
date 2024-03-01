#include "kernel/types.h"
#include "user/user.h"
void
process(int left, int right) {
  int p;
  if(read(left, &p, sizeof(int)) == 0) {
    return;
  }
  printf("prime %d\n", p);
  int num;
  int buf[35];
  int idx = 0;
  while (read(left, &num, sizeof(int)) != 0) {
    if (num % p == 0) {
      continue;
    }
    buf[idx++] = num;
  }
  for (int i = 0; i < idx; i++) {
    write(right, &buf[i], sizeof(int));
  }
}
int
main(int argc, char *argv[])
{
  int numbers[35];
  int idx = 0;
  for (int i = 2; i <= 35; i++) {
    numbers[idx++] = i;
  }
  numbers[idx] = 0;
  while (idx != 0) {
    int left[2], right[2];
    if (pipe(left) == -1 || pipe(right) == -1) {
      exit(1);
    }
    int pid = fork();
    if (pid == -1) {
      exit(1);
    }
    if (pid == 0) {
      // children
      close(left[1]);
      close(right[0]);
      process(left[0], right[1]);
      close(left[0]);
      close(right[1]);
      exit(0);
    } else {
      // parent
      close(left[0]);
      close(right[1]);
      for (int i = 0; i < idx; i++) {
        write(left[1], &numbers[i], sizeof(int));
      }
      close(left[1]);
      idx = 0;
      while (read(right[0], &numbers[idx], sizeof(int)) != 0) {
        idx += 1;
      }
      close(right[0]);
    }
  }
  exit(0);
}