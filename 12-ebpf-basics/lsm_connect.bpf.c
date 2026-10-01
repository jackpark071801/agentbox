#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#define AF_INET 2

struct event {
    __u32 pid;
    __u16 family;
    __u16 port;
    __u32 addr;
};

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 12);
} events SEC(".maps");

SEC("lsm/socket_connect")
int BPF_PROG(handle_socket_connect,
             struct socket *sock,
             struct sockaddr *address,
             int addrlen)
{
    struct event *event;
    struct sockaddr_in *addr4;

    if (!address)
        return 0;

    if (address->sa_family != AF_INET)
        return 0;

    addr4 = (struct sockaddr_in *)address;

    event = bpf_ringbuf_reserve(&events, sizeof(*event), 0);
    if (!event)
        return 0;

    event->pid = bpf_get_current_pid_tgid() >> 32;
    event->family = AF_INET;
    event->port = addr4->sin_port;
    event->addr = addr4->sin_addr.s_addr;

    bpf_ringbuf_submit(event, 0);

    if (addr4->sin_port == 0x5000)
        return -1;

    return 0;
}

char LICENSE[] SEC("license") = "GPL";