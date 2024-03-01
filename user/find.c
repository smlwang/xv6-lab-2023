#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"


char*
basename(char *path)
{
  char *p;

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  return p;
}
void
find(char *path, const char *target)
{
  char *p;
  int fd;
  struct dirent de;
  struct stat st;

  if((fd = open(path, O_RDONLY)) < 0){
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0){
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }
  switch(st.type){
  case T_DEVICE:
  case T_FILE:
    {
      char *name = basename(path);
      // printf("%s: %s: %s\n", name, path, target);
      if (strcmp(name, target) == 0) {
        printf("%s\n", path);
      }
    }
    break;

  case T_DIR:
    if(strlen(path) + 1 + DIRSIZ + 1 > 512){
      printf("find: path too long\n");
      break;
    }
    p = path+strlen(path);
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0)
        continue;
      if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) {
        continue;
      }
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
      find(path, target);
    }
    break;
  }
  close(fd);
}
// return 1, if is dir
// return 0, if is not dir
int
is_dir(char *path)
{
  int fd;
  struct stat st;

  if((fd = open(path, O_RDONLY)) < 0){
    fprintf(2, "find: cannot open %s\n", path);
    exit(1);
  }

  if(fstat(fd, &st) < 0){
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    exit(1);
  }
  if (st.type != T_DIR) {
    return 0;
  }
  return 1;
}
int
main(int argc, char *argv[])
{
  int i;
  char dir[512];

  if(argc < 3){
    dir[0] = '.';
    dir[1] = '\0';
    find(dir, argv[1]);
    exit(0);
  }

  if (strlen(argv[1]) + 1 > 512) {
    printf("find: path too long\n");
    exit(1);
  }
  strcpy(dir, argv[1]);
  if (!is_dir(dir)) {
    fprintf(2, "find: '%s': no such directory\n", dir);
    exit(1);
  }
  for(i=2; i<argc; i++)
    find(dir, argv[i]);
  exit(0);
}
