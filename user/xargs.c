#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  char *args[MAXARG + 1];
  if (argc == 1) {
    fprintf(2, "xargs: not support yet\n");
    exit(1);
  }
  char *program = argv[1];
  int idx = 0;
  for (int i = 1; i < argc; i++) {
    args[idx++] = argv[i];
  }
  char buf[1024];
  char c;
  int len = 0;
  char *p = buf;
  while (read(0, &c, 1)) {
    if (c == '\n') {
      buf[len++] = '\0';
      if (idx >= MAXARG) {
        fprintf(1, "xargs: too many content\n");
        exit(1);
      }
      args[idx++] = p;
      p = buf + len;
    } else {
      if (len + 1 < 1024) {
        buf[len++] = c;
      } else {
        fprintf(2, "xargs: too many content\n");
        exit(1);
      }
    }
  }
  // for (int i = 0; i < idx; i++) {
  //   printf("xargs: %s\n", args[i]);
  // }
  args[idx] = 0;
  int pid = fork();
  if (pid == -1) {
    fprintf(2, "xargs: can not fork\n");
    exit(1);
  }
  if (pid == 0) {
    // children
    if (exec(program, (char**)args) == -1) {
      fprintf(2, "xargs: faild to exec\n");
      exit(1);
    }
  } else {
    // parent
    wait(0);
  }
  exit(0);
}
