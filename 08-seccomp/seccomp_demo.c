#include <errno.h>
#include <seccomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void)
{

    printf("Sandboxed process PID: %d\n", getpid());

    scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_ALLOW);
    if (ctx == NULL) {
        perror("seccomp_init");
        return 1;
    }

    /*
     * Instead of automatically allowing or denying mkdirat(),
     * ask a userspace supervisor what to do.
     */
    if (seccomp_rule_add(ctx,
                         SCMP_ACT_NOTIFY,
                         SCMP_SYS(mkdirat),
                         0) < 0) {
        fprintf(stderr, "Failed to add notification rule\n");
        seccomp_release(ctx);
        return 1;
    }

    if (seccomp_load(ctx) < 0) {
        fprintf(stderr, "Failed to load seccomp filter\n");
        seccomp_release(ctx);
        return 1;
    }

    int notify_fd = seccomp_notify_fd(ctx);

    if (notify_fd < 0) {
        fprintf(stderr, "Failed to get notification fd\n");
        seccomp_release(ctx);
        return 1;
    }

    printf("Seccomp notification filter installed.\n");
    printf("Notification FD: %d\n", notify_fd);
    printf("PID: %d\n", getpid());

    /*
     * Pass the notification FD to the supervisor through stdout.
     *
     * For this first experiment we'll simply print it and keep
     * the process alive. The supervisor will receive the FD
     * through the parent/child setup we'll build next.
     */
    printf("Waiting for supervisor...\n");
    fflush(stdout);

    /*
     * In this first version, we don't actually trigger mkdirat yet.
     */
    sleep(30);

    seccomp_release(ctx);
    return 0;
}
