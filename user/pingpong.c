#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    int pipes[2];
    if (pipe(pipes) == -1) {
        exit(1);
    }
    int stat = fork();
    if (stat == -1) {
        exit(1);
    }
    char byte = 0; // byte to transmit
    if (stat == 0) {
        // children
        read(pipes[0], &byte, 1);
        printf("%d: received ping\n", getpid());
        close(pipes[0]);
        write(pipes[1], &byte, 1);
        close(pipes[1]);
    } else {
        // parent
        write(pipes[1], &byte, 1);
        close(pipes[1]);
        read(pipes[0], &byte, 1);
        printf("%d: received pong\n", getpid());
        close(pipes[0]);
    }
    exit(0);
}