#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>

struct event {
    int pid;
    int ppid;
    int ret;
    char filename[256];
};

struct trace_event_raw_sys_enter_execve {
    unsigned short common_type;
    unsigned char common_flags;
    unsigned char common_preempt_count;
    int common_pid;
    int __syscall_nr;
    const char *filename;
    const char *const *argv;
    const char *const *envp;
};

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 12);
} events SEC(".maps");

SEC("tracepoint/syscalls/sys_enter_execve")
int handle_execve(void *ctx)
{
    struct event *event;
    int pid;
    int ppid;
    int ret;
    const char *filename;
    struct task_struct *task;

    event = bpf_ringbuf_reserve(&events, sizeof(*event), 0);
    if (!event)
        return 0;

    pid = bpf_get_current_pid_tgid() >> 32;
    event->pid = pid;
    
    task = (struct task_struct *)bpf_get_current_task();
    event->ppid = BPF_CORE_READ(task, real_parent, tgid);

    struct trace_event_raw_sys_enter_execve *args = ctx;

    filename = args->filename;

    ret = bpf_probe_read_user_str(
        event->filename,
        sizeof(event->filename),
        filename
    );

    event->ret = ret;

    bpf_ringbuf_submit(event, 0);

    return 0;
}

char LICENSE[] SEC("license") = "GPL";