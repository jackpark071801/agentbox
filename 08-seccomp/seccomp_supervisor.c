#include <errno.h>
#include <seccomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int send_fd(int socket_fd, int fd)
{
    char data = 'F';

    struct iovec iov = {
        .iov_base = &data,
        .iov_len = sizeof(data)
    };

    char control[CMSG_SPACE(sizeof(int))];

    memset(control, 0, sizeof(control));

    struct msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = control,
        .msg_controllen = sizeof(control)
    };

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);

    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));

    memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));

    msg.msg_controllen = cmsg->cmsg_len;

    return sendmsg(socket_fd, &msg, 0);
}

static int receive_fd(int socket_fd)
{
    char data;

    struct iovec iov = {
        .iov_base = &data,
        .iov_len = sizeof(data)
    };

    char control[CMSG_SPACE(sizeof(int))];

    memset(control, 0, sizeof(control));

    struct msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = control,
        .msg_controllen = sizeof(control)
    };

    if (recvmsg(socket_fd, &msg, 0) < 0) {
        perror("recvmsg");
        return -1;
    }

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);

    if (cmsg == NULL ||
        cmsg->cmsg_level != SOL_SOCKET ||
        cmsg->cmsg_type != SCM_RIGHTS) {
        fprintf(stderr, "No file descriptor received\n");
        return -1;
    }

    int fd;
    memcpy(&fd, CMSG_DATA(cmsg), sizeof(fd));

    return fd;
}

static int install_filter(void)
{
    scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_ALLOW);

    if (ctx == NULL) {
        perror("seccomp_init");
        return -1;
    }

    if (seccomp_rule_add(ctx,
                         SCMP_ACT_NOTIFY,
                         SCMP_SYS(mkdirat),
                         0) < 0) {
        fprintf(stderr, "Failed to add seccomp rule\n");
        seccomp_release(ctx);
        return -1;
    }

    if (seccomp_load(ctx) < 0) {
        fprintf(stderr, "Failed to load seccomp filter\n");
        seccomp_release(ctx);
        return -1;
    }

    return seccomp_notify_fd(ctx);
}

int main(void)
{
    int sockets[2];

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) < 0) {
        perror("socketpair");
        return 1;
    }

    pid_t child = fork();

    if (child < 0) {
        perror("fork");
        return 1;
    }

    if (child == 0) {
        close(sockets[0]);

        printf("[sandbox] PID: %d\n", getpid());

        int notify_fd = install_filter();

        if (notify_fd < 0) {
            return 1;
        }

        printf("[sandbox] Seccomp filter installed\n");
        printf("[sandbox] Sending notification FD to supervisor\n");
        fflush(stdout);

        if (send_fd(sockets[1], notify_fd) < 0) {
            perror("send_fd");
            return 1;
        }

        close(sockets[1]);

        printf("[sandbox] Calling mkdirat()\n");
        fflush(stdout);

        if (mkdir("/tmp/seccomp-supervised", 0755) == 0) {
            printf("[sandbox] mkdir succeeded\n");
        } else {
            printf("[sandbox] mkdir failed: %s\n", strerror(errno));
        }

        return 0;
    }

    close(sockets[1]);

    printf("[supervisor] PID: %d\n", getpid());
    printf("[supervisor] Waiting for seccomp notification FD...\n");

    int notify_fd = receive_fd(sockets[0]);

    if (notify_fd < 0) {
        return 1;
    }

    printf("[supervisor] Received notification FD: %d\n", notify_fd);

    struct seccomp_notif *req = NULL;
    struct seccomp_notif_resp *resp = NULL;

    if (seccomp_notify_alloc(&req, &resp) < 0) {
        fprintf(stderr, "Failed to allocate notification structures\n");
        return 1;
    }

    printf("[supervisor] Waiting for syscall notification...\n");
    fflush(stdout);

    if (seccomp_notify_receive(notify_fd, req) < 0) {
        perror("seccomp_notify_receive");
        return 1;
    }

    printf("[supervisor] Received syscall notification!\n");
    printf("[supervisor] Syscall number: %d\n", req->data.nr);
    printf("[supervisor] Process ID: %d\n", req->pid);

    /*
     * Deny the syscall.
     */
    resp->id = req->id;
    resp->val = -1;
    resp->error = -EPERM;
    resp->flags = 0;

    printf("[supervisor] DENYING syscall\n");

    if (seccomp_notify_respond(notify_fd, resp) < 0) {
        perror("seccomp_notify_respond");
        return 1;
    }

    seccomp_notify_free(req, resp);

    close(notify_fd);

    waitpid(child, NULL, 0);

    printf("[supervisor] Sandbox exited\n");

    return 0;
}
