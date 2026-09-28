#include <stdio.h>
#include <unistd.h>
#include <bpf/libbpf.h>

struct event {
    int pid;
    int ppid;
    int ret;
    char filename[256];
};

static int handle_event(void *ctx, void *data, size_t data_sz)
{
    struct event *event = data;

    printf("execve: pid=%d ppid=%d ret=%d filename=%s\n",
       event->pid,
       event->ppid,
       event->ret,
       event->filename);

    return 0;
}

int main(void)
{
    struct bpf_object *obj;

    obj = bpf_object__open_file("hello.bpf.o", NULL);
    if (!obj) {
        fprintf(stderr, "Failed to open BPF object\n");
        return 1;
    }

    printf("BPF object opened successfully.\n");

    if (bpf_object__load(obj) != 0) {
        fprintf(stderr, "Failed to load BPF object\n");
        bpf_object__close(obj);
        return 1;
    }

    printf("BPF object loaded into the kernel.\n");
    struct bpf_program *prog;
    prog = bpf_object__next_program(obj, NULL);

    struct bpf_link *link;
    link = bpf_program__attach_tracepoint(prog, "syscalls", "sys_enter_execve");

    if (!link) {
        fprintf(stderr, "Failed to attach BPF program\n");
        bpf_object__close(obj);
        return 1;
    }

    printf("BPF program attached to sys_enter_execve.\n");

    struct ring_buffer *rb;

    rb = ring_buffer__new(
        bpf_map__fd(bpf_object__find_map_by_name(obj, "events")),
        handle_event,
        NULL,
        NULL
    );

    if (!rb) {
        fprintf(stderr, "Failed to create ring buffer\n");
        bpf_link__destroy(link);
        bpf_object__close(obj);
        return 1;
    }

    printf("Ring buffer created.\n");
    printf("Waiting for execve events...\n");

    while (1) {
        ring_buffer__poll(rb, 100);
    }
}