#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>

#include "lsm_connect.skel.h"

struct event {
    __u32 pid;
    __u16 family;
    __u16 port;
    __u32 addr;
};

static int handle_event(void *ctx, void *data, size_t data_sz)
{
    (void)ctx;
    (void)data_sz;

    struct event *event = data;

    char ip[INET_ADDRSTRLEN];

    if (inet_ntop(
            AF_INET,
            &event->addr,
            ip,
            sizeof(ip)) == NULL) {
        snprintf(ip, sizeof(ip), "unknown");
    }

    printf(
        "socket_connect: pid=%u destination=%s:%u\n",
        event->pid,
        ip,
        ntohs(event->port)
    );

    return 0;
}

int main(void)
{
    struct lsm_connect_bpf *skel;

    skel = lsm_connect_bpf__open_and_load();
    if (!skel) {
        fprintf(stderr, "Failed to open and load BPF program\n");
        return 1;
    }

    printf("BPF LSM program loaded successfully.\n");

    skel->links.handle_socket_connect =
        bpf_program__attach_lsm(skel->progs.handle_socket_connect);

    if (!skel->links.handle_socket_connect) {
        fprintf(stderr, "Failed to attach BPF LSM program\n");
        lsm_connect_bpf__destroy(skel);
        return 1;
    }

    printf("BPF LSM program attached to socket_connect.\n");
    printf("PID: %d\n", getpid());
    printf("Waiting for socket connections...\n");

    struct ring_buffer *rb;

    rb = ring_buffer__new(
        bpf_map__fd(skel->maps.events),
        handle_event,
        NULL,
        NULL
    );

    if (!rb) {
        fprintf(stderr, "Failed to create ring buffer\n");
        lsm_connect_bpf__destroy(skel);
        return 1;
    }

    while (1) {
        ring_buffer__poll(rb, 100);
    }

    ring_buffer__free(rb);
    lsm_connect_bpf__destroy(skel);

    return 0;
}