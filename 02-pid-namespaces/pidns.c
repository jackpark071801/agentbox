#define _GNU_SOURCE

#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

#define STACK_SIZE (1024 * 1024)

static char child_stack[STACK_SIZE];

int child_main(void *arg) {
    printf("Hello from namespace!\n");
    printf("PID: %d\n", getpid());
    printf("Parent PID: %d\n", getppid());

    execlp("bash", "bash", NULL);

    perror("execlp");
    return 1;
}

int main(void) {
    printf("Parent PID: %d\n", getpid());

    int flags = CLONE_NEWPID | SIGCHLD;

    pid_t child = clone(
        child_main,
        child_stack + STACK_SIZE,
        flags,
        NULL
    );

    if (child == -1) {
        perror("clone");
        return 1;
    }

    waitpid(child, NULL, 0);

    return 0;
}
