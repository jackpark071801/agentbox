# eBPF Basics

This experiment introduces **eBPF (extended Berkeley Packet Filter)** as a Linux kernel instrumentation and policy mechanism.

The goal is not to build a complete sandbox yet. Instead, this experiment establishes the basic eBPF execution pipeline:

```text
C source
   │
   ▼
clang
   │
   ▼
eBPF ELF object
   │
   ▼
libbpf
   │
   ▼
BPF syscall
   │
   ▼
kernel verifier
   │
   ▼
loaded eBPF program
   │
   ▼
tracepoint attachment
   │
   ▼
kernel event
   │
   ▼
ring buffer
   │
   ▼
userspace loader
```

The experiment eventually records:

* the PID of a process executing a program
* its parent PID
* the executable filename
* the length returned by `bpf_probe_read_user_str()`

---

## 1. Why eBPF matters for AgentBox

A sandbox needs mechanisms that can observe and potentially control what an agent does.

Earlier experiments explored mechanisms at different layers:

```text
Processes
PID namespaces
User namespaces
Mount namespaces
Root filesystem
cgroups
Capabilities
seccomp
Landlock
Network namespaces
iptables
HTTP proxy
HTTPS MITM
```

eBPF provides another mechanism.

Unlike an application proxy, eBPF programs can execute inside the Linux kernel at defined attachment points.

For example, an eBPF program can observe events such as:

```text
process execution
system calls
network packets
socket operations
kernel tracepoints
kernel function calls
security hooks
```

This makes eBPF interesting for sandbox monitoring and policy enforcement.

However, eBPF is **not itself a sandbox policy**.

It is a programmable kernel mechanism. The policy is something we build using that mechanism.

---

# 2. The basic eBPF model

An eBPF program is compiled separately from the normal userspace program.

Our source file is:

```text
hello.bpf.c
```

It is compiled with:

```bash
clang -O2 -g -target bpf \
  -I/usr/include/aarch64-linux-gnu \
  -c hello.bpf.c \
  -o hello.bpf.o
```

The important option is:

```text
-target bpf
```

This tells Clang to generate **eBPF instructions**, rather than ARM64 machine code.

The resulting object file is an ELF object containing an eBPF program and metadata used by the loader.

---

# 3. eBPF is not ARM64

The development machine uses Apple Silicon and the Docker container reports:

```text
aarch64
```

It is tempting to think that compiling inside the container produces ARM64 instructions.

It does not.

The command:

```bash
clang -target bpf ...
```

produces instructions for the **eBPF virtual machine**.

We verified this with:

```bash
llvm-objdump -d hello.bpf.o
```

which reported:

```text
file format elf64-bpf
```

The kernel later verifies and executes these eBPF instructions.

Conceptually:

```text
C
 │
 ▼
Clang
 │
 ▼
eBPF instructions
 │
 ▼
Linux kernel
```

The container's CPU architecture is therefore separate from the instruction set used by the BPF program.

---

# 4. The kernel verifier

An eBPF program cannot simply be copied into the kernel and executed like arbitrary kernel code.

When the program is loaded, the Linux kernel runs it through the **BPF verifier**.

The verifier checks properties such as:

* memory accesses
* pointer usage
* register state
* control flow
* helper arguments
* bounds
* whether execution can reach invalid states

The purpose is to prevent an eBPF program from arbitrarily corrupting kernel memory or otherwise violating kernel safety rules.

The high-level loading path is:

```text
userspace
    │
    │ BPF syscall
    ▼
kernel
    │
    ▼
BPF verifier
    │
    ├── reject
    │
    └── accept
          │
          ▼
     loaded program
```

Our program successfully passed this process.

---

# 5. Tracepoints

Our first attachment point is:

```text
syscalls/sys_enter_execve
```

This is a kernel tracepoint that fires when a process enters the `execve` system call.

The BPF section declares the attachment point:

```c
SEC("tracepoint/syscalls/sys_enter_execve")
int handle_execve(void *ctx)
{
    ...
}
```

The userspace loader then explicitly attaches the program:

```c
link = bpf_program__attach_tracepoint(
    prog,
    "syscalls",
    "sys_enter_execve"
);
```

The resulting relationship is:

```text
process
   │
   │ execve()
   ▼
sys_enter_execve tracepoint
   │
   ▼
eBPF program
```

This means the BPF program runs whenever the tracepoint fires.

---

# 6. Inspecting the tracepoint

Before writing the program, we inspected the tracepoint definition.

Tracefs was not initially mounted in the container.

We found that the kernel supports tracefs:

```bash
cat /proc/filesystems | grep trace
```

which showed:

```text
nodev   tracefs
```

We mounted it with:

```bash
mount -t tracefs tracefs /sys/kernel/tracing
```

After mounting it, syscall tracepoints became visible:

```bash
ls /sys/kernel/tracing/events/syscalls
```

We then inspected:

```bash
cat /sys/kernel/tracing/events/syscalls/sys_enter_execve/format
```

The relevant fields were:

```text
field:int common_pid;
field:int __syscall_nr;
field:const char * filename;
field:const char *const * argv;
field:const char *const * envp;
```

This showed us the layout of the tracepoint context.

---

# 7. Reading the tracepoint context

We defined a matching structure in the BPF program:

```c
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
```

The handler treats the tracepoint context as this structure:

```c
struct trace_event_raw_sys_enter_execve *args = ctx;

filename = args->filename;
```

The important distinction is that:

```c
args->filename
```

is a **pointer to userspace memory**.

It is not the actual filename string stored inside the BPF context.

---

# 8. Reading userspace memory

The BPF program therefore uses:

```c
bpf_probe_read_user_str(
    event->filename,
    sizeof(event->filename),
    filename
);
```

This safely copies the userspace string into our BPF event buffer.

The return value is stored in:

```c
event->ret
```

For a successful read, the return value includes the terminating NUL byte.

For example:

```text
/bin/true
```

contains 9 characters plus the terminating NUL:

```text
ret = 10
```

Similarly:

```text
/bin/echo
```

produces:

```text
ret = 10
```

and:

```text
/bin/ls
```

produces:

```text
ret = 8
```

We also encountered:

```text
ret = -14
```

during an earlier version of the experiment.

`-14` is `-EFAULT`, indicating an invalid memory address was supplied to the user-memory read.

That failure led us to inspect the actual tracepoint context instead of assuming the pointer layout.

---

# 9. BPF maps and the ring buffer

The BPF program needs a way to communicate information back to userspace.

We use a BPF ring buffer:

```c
struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 12);
} events SEC(".maps");
```

The program reserves space:

```c
event = bpf_ringbuf_reserve(
    &events,
    sizeof(*event),
    0
);
```

It fills the event and submits it:

```c
bpf_ringbuf_submit(event, 0);
```

Conceptually:

```text
             Linux kernel
                  │
             eBPF program
                  │
                  ▼
           ┌─────────────┐
           │ ring buffer │
           └──────┬──────┘
                  │
                  ▼
             userspace
```

The ring buffer is therefore the communication channel between the kernel-resident BPF program and our userspace loader.

---

# 10. The event structure

The kernel-side event is:

```c
struct event {
    int pid;
    int ppid;
    int ret;
    char filename[256];
};
```

The userspace loader defines the same layout:

```c
struct event {
    int pid;
    int ppid;
    int ret;
    char filename[256];
};
```

The layout is:

```text
offset 0      pid
offset 4      ppid
offset 8      ret
offset 12     filename[256]
```

The ring buffer transfers bytes.

It does not automatically understand C structures.

Therefore, the kernel-side and userspace-side definitions must agree.

---

# 11. Getting the current PID

The BPF helper:

```c
bpf_get_current_pid_tgid()
```

returns a 64-bit value containing process identifiers.

We use:

```c
pid = bpf_get_current_pid_tgid() >> 32;
```

and store it in:

```c
event->pid
```

This gives the thread-group ID used as the process PID.

---

# 12. BTF

The next part of the experiment introduces **BTF**.

BTF stands for:

```text
BPF Type Format
```

It provides type information about the running kernel.

Our kernel exposes its BTF data here:

```text
/sys/kernel/btf/vmlinux
```

We verified that this file exists.

BTF allows tools and BPF programs to understand kernel types such as:

```text
struct task_struct
```

without manually maintaining kernel structure definitions.

---

# 13. Generating vmlinux.h

We generated a C header from the kernel's BTF data:

```bash
/usr/lib/linux-tools/6.8.0-139-generic/bpftool \
  btf dump file /sys/kernel/btf/vmlinux format c \
  > vmlinux.h
```

The resulting file was approximately:

```text
3.4 MB
```

It contains C definitions derived from the running kernel's BTF information.

Our BPF program includes it:

```c
#include "vmlinux.h"
```

This is different from including:

```c
#include <linux/bpf.h>
```

The latter provides BPF-related definitions, while `vmlinux.h` provides the kernel type information needed for CO-RE.

---

# 14. CO-RE

CO-RE stands for:

```text
Compile Once — Run Everywhere
```

The goal is to make BPF programs more portable across kernels whose internal structure layouts may differ.

Instead of hard-coding something like:

```text
real_parent is always at byte offset X
```

we write a field-based access:

```c
BPF_CORE_READ(task, real_parent, tgid)
```

BTF describes the kernel's types and fields, while CO-RE relocation information allows libbpf to adapt the generated program to the target kernel.

The conceptual flow is:

```text
BPF source
    │
    │ BPF_CORE_READ()
    ▼
Clang
    │
    ▼
CO-RE relocation information
    │
    ▼
libbpf
    │
    ▼
target kernel BTF
    │
    ▼
correct kernel field access
```

---

# 15. Walking from the current task to the parent

The BPF program obtains the current task:

```c
struct task_struct *task;

task = (struct task_struct *)bpf_get_current_task();
```

It then reads:

```c
event->ppid = BPF_CORE_READ(
    task,
    real_parent,
    tgid
);
```

Conceptually:

```text
current task_struct
       │
       ▼
   real_parent
       │
       ▼
parent task_struct
       │
       ▼
      tgid
```

This is our first experiment using BTF/CO-RE to traverse a Linux kernel structure.

---

# 16. Why `bpf_core_read.h` wasn't enough

Initially we included:

```c
#include <bpf/bpf_core_read.h>
```

but Clang still reported:

```text
incomplete definition of type 'struct task_struct'
```

The reason is that the header provides the CO-RE machinery, but it does not itself contain the complete definition of the kernel's `task_struct`.

We solved this by generating:

```text
vmlinux.h
```

from the kernel's BTF and including:

```c
#include "vmlinux.h"
```

This gave Clang the kernel type definitions required to compile the CO-RE access.

---

# 17. Userspace loader

The userspace program is:

```text
loader.c
```

It uses libbpf.

The major steps are:

```c
bpf_object__open_file(...)
```

Open the compiled BPF object.

Then:

```c
bpf_object__load(...)
```

Load the program into the kernel.

Then:

```c
bpf_program__attach_tracepoint(...)
```

Attach it to:

```text
sys_enter_execve
```

Then:

```c
ring_buffer__new(...)
```

Create a userspace consumer for the BPF ring buffer.

Finally:

```c
ring_buffer__poll(...)
```

waits for events.

The overall userspace sequence is:

```text
hello.bpf.o
     │
     ▼
open
     │
     ▼
load
     │
     ▼
attach
     │
     ▼
create ring buffer
     │
     ▼
poll
```

---

# 18. The complete event pipeline

When a process executes a program:

```text
/bin/ls
```

the kernel enters the `execve` syscall.

The tracepoint fires:

```text
sys_enter_execve
```

Our BPF program runs:

```text
sys_enter_execve
       │
       ▼
handle_execve()
       │
       ├── current PID
       │
       ├── current task_struct
       │       │
       │       └── real_parent → parent tgid
       │
       ├── filename pointer
       │
       └── bpf_probe_read_user_str()
                    │
                    ▼
             event structure
                    │
                    ▼
              ring buffer
                    │
                    ▼
               loader.c
                    │
                    ▼
        pid / ppid / filename
```

The observed output was:

```text
execve: pid=1217 ppid=995 ret=15 filename=/usr/bin/clear
execve: pid=1218 ppid=995 ret=10 filename=/bin/true
execve: pid=1219 ppid=995 ret=10 filename=/bin/echo
execve: pid=1220 ppid=995 ret=8 filename=/bin/ls
```

This demonstrated the complete kernel-to-userspace event pipeline.

---

# 19. What this experiment proves

### eBPF compilation

```bash
clang -target bpf ...
```

produces an eBPF object rather than normal ARM64 executable code.

### Kernel loading

```c
bpf_object__load(...)
```

successfully loaded the BPF program into the Linux kernel.

### Tracepoint attachment

```c
bpf_program__attach_tracepoint(...)
```

attached the program to `sys_enter_execve`.

### Kernel event observation

Executing commands generated events received by our BPF program.

### Ring buffer communication

The kernel-resident BPF program successfully transferred structured events to userspace.

### Userspace memory reading

`bpf_probe_read_user_str()` successfully copied the `execve` filename.

### BTF

The kernel exposed BTF through:

```text
/sys/kernel/btf/vmlinux
```

### CO-RE

`BPF_CORE_READ()` successfully traversed:

```text
task_struct
    → real_parent
        → tgid
```

and produced the expected parent PID.

---

# 20. Inspecting loaded BPF programs

The container's `bpftool` command is an Ubuntu wrapper that expects a bpftool binary matching the running kernel version.

Docker Desktop is using a LinuxKit kernel:

```text
7.0.12-linuxkit
```

The wrapper therefore failed to find:

```text
bpftool for kernel 7.0.12
```

However, the actual installed bpftool binary was available at:

```text
/usr/lib/linux-tools/6.8.0-139-generic/bpftool
```

We used that binary directly.

For example:

```bash
/usr/lib/linux-tools/6.8.0-139-generic/bpftool prog show
```

showed our loaded program as a tracepoint program:

```text
tracepoint  name handle_execve
```

This demonstrated that the program was actually resident in the kernel.

---

# 21. Docker Desktop caveat

This lab is running inside a privileged Docker container.

That means the environment is useful for learning kernel mechanisms, but it is **not equivalent to a production sandbox security boundary**.

The actual kernel belongs to Docker Desktop's Linux environment:

```text
Docker container
       │
       ▼
Docker Desktop LinuxKit VM
       │
       ▼
Linux kernel
```

The BPF program therefore runs in the LinuxKit kernel used by Docker Desktop.

This distinction matters when experimenting with:

* namespaces
* cgroups
* capabilities
* seccomp
* eBPF
* kernel tracing
* network configuration

The behavior observed here is Linux behavior, but some permissions and kernel facilities are constrained by the surrounding Docker environment.

---

# 22. Why eBPF is interesting for AgentBox

The current program only observes process execution.

A future AgentBox architecture could potentially use eBPF to observe events such as:

```text
process execution
        │
        ▼
   network activity
        │
        ▼
  socket operations
        │
        ▼
  security events
```

For example, an AgentBox supervisor could potentially maintain an event stream like:

```text
PID 100
  ├── exec /usr/bin/python
  ├── connect 1.1.1.1:443
  ├── exec /usr/bin/curl
  └── connect 8.8.8.8:53
```

That information could be used for:

* auditing
* monitoring
* anomaly detection
* policy decisions
* debugging
* enforcement when appropriate hooks are available

But eBPF should not automatically replace the other sandbox mechanisms.

A realistic sandbox may combine several layers:

```text
                 Agent
                   │
        ┌──────────┴──────────┐
        │                     │
   filesystem             process
   restrictions           restrictions
        │                     │
    Landlock              seccomp
        │                     │
        └──────────┬──────────┘
                   │
              namespaces
                   │
             network policy
                   │
               eBPF
                   │
             application
                proxy
```

Each mechanism operates at a different layer and provides different visibility and enforcement properties.

---

# 23. Important limitation: eBPF is not automatically HTTP-aware

The current eBPF program sees process execution.

It does **not** automatically understand:

```text
HTTP method
HTTP hostname
URL path
HTTP request body
HTTPS contents
```

For example:

```text
curl https://example.com/admin
```

may result in observable process and socket activity, but the BPF program does not automatically see:

```text
GET /admin
Host: example.com
```

The HTTPS payload is encrypted.

This is one reason AgentBox may need both:

```text
eBPF / kernel-level controls
```

and:

```text
application-layer proxy
```

The earlier HTTPS MITM experiment operates at a different layer.

---

# 24. Current files

The experiment contains:

```text
12-ebpf-basics/
├── README.md
├── hello.bpf.c
├── loader.c
└── vmlinux.h
```

Build artifacts are intentionally ignored:

```text
hello.bpf.o
loader
```

The `.gitignore` contains:

```text
# eBPF experiment build artifacts
12-ebpf-basics/hello.bpf.o
12-ebpf-basics/loader
```

`vmlinux.h` is intentionally tracked because it provides the kernel type definitions required to build this CO-RE example.

---

# 25. Key concepts learned

This experiment introduced:

```text
eBPF
BPF verifier
BPF helpers
BPF maps
ring buffers
tracepoints
libbpf
BTF
CO-RE
vmlinux.h
task_struct
process lineage
kernel → userspace event delivery
```

The most important conceptual distinction is:

```text
eBPF
  =
programmable kernel mechanism

Policy
  =
rules implemented using mechanisms
```

eBPF becomes useful to AgentBox because it can provide programmable kernel-level observation and, at suitable attachment points, enforcement.

---

# 26. Next direction

The next eBPF experiments should build incrementally from this one.

Possible directions include:

```text
1. Observe process execution more completely
2. Track process ancestry
3. Observe socket creation
4. Observe network connections
5. Explore cgroup-attached BPF
6. Explore BPF-based network policy
7. Explore security/LSM hooks
8. Combine eBPF observation with AgentBox policy
```

The important architectural question is no longer simply:

> "Can we run eBPF?"

We have demonstrated that we can.

The next question is:

> **Where does eBPF fit alongside namespaces, seccomp, Landlock, network policy, and the application proxy in an AgentBox sandbox?**
