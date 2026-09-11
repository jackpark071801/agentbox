#define _GNU_SOURCE

#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mount.h>

int main(void)
{
    printf("Before unshare:\n");
    printf("PID: %d\n", getpid());

    if (unshare(CLONE_NEWNS) == -1) {
        perror("unshare");
        return 1;
    }

    printf("\nAfter unshare:\n");
    printf("PID: %d\n", getpid());

    printf("\nMounting tmpfs...\n");

    if (mount("tmpfs",
              "/tmp/agentbox-c-test",
              "tmpfs",
              0,
              NULL) == -1) {
        perror("mount");
        return 1;
    }

    printf("Mounted successfully.\n");

    pause();

    return 0;
}
