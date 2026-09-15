# Seccomp

Seccomp (secure computing mode) restricts the Linux system calls a process can make.

The basic idea is:

    application
        |
        | syscall
        v
    seccomp filter
        |
        +---- allow ----> kernel
        |
        +---- deny -----> error / kill
        |
        +---- notify ---> userspace supervisor

## Basic filter

The first experiment used libseccomp to create a filter with:

    SCMP_ACT_ALLOW

as the default action.

A rule then denied `mkdirat`:

    SCMP_ACT_ERRNO(EPERM)

This demonstrated that a process can remain running while the kernel rejects a specific syscall.

### Important: libc functions are not necessarily syscalls

The C function:

    mkdir()

does not necessarily correspond directly to the kernel syscall:

    SYS_mkdir

On this system, the operation uses:

    SYS_mkdirat

Therefore filtering `SCMP_SYS(mkdir)` did not stop the `mkdir()` C library call.

Filtering:

    SCMP_SYS(mkdirat)

did stop it.

This demonstrates that seccomp operates at the kernel syscall boundary, not at the level of C library functions.

## Observing seccomp

Before installing a filter:

    grep Seccomp /proc/self/status

showed:

    Seccomp:        0
    Seccomp_filters:        0

For a process with our filter installed:

    Seccomp:        2
    Seccomp_filters:        1

`Seccomp: 2` means the process is using seccomp filter mode.

## Seccomp user notification

The second experiment used:

    SCMP_ACT_NOTIFY

Instead of automatically allowing or denying `mkdirat`, the kernel generated a notification for a userspace supervisor.

The architecture was:

    sandbox process
          |
          | mkdirat()
          v
       seccomp
          |
          | notification
          v
      supervisor
          |
          +---- allow ----> continue syscall
          |
          +---- deny -----> EPERM

The sandbox sent the seccomp notification file descriptor to the supervisor using a Unix socket and `SCM_RIGHTS`.

The supervisor then used the libseccomp notification API to:

1. Receive the syscall notification.
2. Inspect the syscall number and process ID.
3. Decide whether to allow or deny the operation.
4. Return the decision to the kernel.

The allow case used:

    SECCOMP_USER_NOTIF_FLAG_CONTINUE

The deny case returned:

    -EPERM

The negative errno is important because seccomp notification responses represent errors as negative errno values.

## Security considerations

Seccomp is powerful, but a syscall notification supervisor is not automatically a safe general-purpose policy engine.

In particular, making security decisions based on userspace inspection of syscall arguments can introduce TOCTOU (time-of-check/time-of-use) problems and other subtleties.

For example, a supervisor should not blindly assume that a pathname observed during a notification remains associated with the same object when the syscall actually executes.

For filesystem policy, mechanisms such as Landlock can provide stronger kernel-enforced object-level restrictions.

## Experiments

### `seccomp_demo.c`

Demonstrates:

- Creating a seccomp filter with libseccomp.
- Allowing syscalls by default.
- Denying `mkdirat` with `SCMP_ACT_ERRNO`.
- Observing the resulting `EPERM`.

### `seccomp_supervisor.c`

Demonstrates:

- `SCMP_ACT_NOTIFY`.
- Receiving syscall notifications in userspace.
- Passing a notification file descriptor over a Unix socket.
- Allowing a syscall with `SECCOMP_USER_NOTIF_FLAG_CONTINUE`.
- Denying a syscall with `-EPERM`.

## Key takeaway

Namespaces restrict what a process can see.

Capabilities restrict privileged operations.

Seccomp restricts which syscalls a process can make.

Together they form several complementary layers of a Linux sandbox.
