# Landlock

This experiment demonstrates Linux Landlock filesystem access control.

Landlock provides kernel-enforced restrictions on filesystem access for
unprivileged processes. Unlike seccomp, which filters system calls,
Landlock controls what filesystem objects a process can access and what
operations it can perform on them.

## Why Landlock?

Our sandbox now has several different layers:

- Namespaces isolate what a process can see.
- Capabilities restrict privileged operations.
- Seccomp restricts system calls.
- Landlock restricts filesystem access.

A useful mental model is:

```text
Agent process
     |
     +-- namespaces   -> what exists in its view
     |
     +-- capabilities -> privileged operations
     |
     +-- seccomp      -> which syscalls are allowed
     |
     +-- Landlock     -> which filesystem objects are accessible

