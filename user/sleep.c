#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if (argc != 2) {
    const char *usage = "Usage: sleep <seconds>\n";
    write(1, usage, strlen(usage));
    exit(1);
  }
  int times = atoi(argv[1]);
  sleep(times);
  exit(0);
}
