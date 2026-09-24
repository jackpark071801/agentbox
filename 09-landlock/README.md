# Landlock

This experiment demonstrates Linux Landlock filesystem access control.

Landlock is a Linux security mechanism that lets a process voluntarily
restrict its own filesystem access. The restrictions are enforced by the
kernel.

Unlike seccomp, which operates at the syscall boundary, Landlock expresses
policy in terms of filesystem objects and filesystem operations.

## Why Landlock?

Our sandbox now has several complementary layers:

```text
Agent process
     |
     +-- namespaces   -> what exists in its view
     |
     +-- capabilities -> privileged operations
     |
     +-- seccomp      -> which syscalls are allowed
     |
     +-- Landlock     -> which filesystem objects and
                         operations are allowed
```

The important distinction is:

```
seccomp
    controls which system calls a process can make

Landlock
    controls which filesystem operations the process can perform
    on particular filesystem objects
```

## Checking Kernel Support

Landlock is not a filesystem type, so checking:

```
grep landlock /proc/filesystems
```

is not a valid way to determine whether Landlock is supported.

Instead, the Landlock API is exposed through Linux system calls.

The Linux headers were available at:

```
/usr/include/linux/landlock.h
```

The experiment directly queried the Landlock ABI version.

The kernel reported:

```
Landlock ABI version: 8
```

This confirms that the running kernel supports Landlock and exposes
Landlock ABI version 8.

## Landlock Rulesets

A Landlock policy begins with a ruleset.

The ruleset specifies which filesystem access types the process wants
to restrict.

The experiment used access types including:

```
LANDLOCK_ACCESS_FS_EXECUTE
LANDLOCK_ACCESS_FS_READ_FILE
LANDLOCK_ACCESS_FS_READ_DIR
LANDLOCK_ACCESS_FS_WRITE_FILE
LANDLOCK_ACCESS_FS_REMOVE_DIR
LANDLOCK_ACCESS_FS_REMOVE_FILE
LANDLOCK_ACCESS_FS_MAKE_CHAR
LANDLOCK_ACCESS_FS_MAKE_DIR
LANDLOCK_ACCESS_FS_MAKE_REG
LANDLOCK_ACCESS_FS_MAKE_SOCK
LANDLOCK_ACCESS_FS_MAKE_FIFO
LANDLOCK_ACCESS_FS_MAKE_BLOCK
LANDLOCK_ACCESS_FS_MAKE_SYM
LANDLOCK_ACCESS_FS_REFER
LANDLOCK_ACCESS_FS_TRUNCATE
```

The ruleset does not automatically define which directories are
allowed.

Instead, rules are added to the ruleset describing which filesystem
subtrees are permitted.

## Path-Beneath Rules

The experiment used:

```
LANDLOCK_RULE_PATH_BENEATH
```

This allows a rule to describe a filesystem hierarchy rooted at a
specific directory.

Conceptually:

```
/workspace/09-landlock/allowed/
    |
    +-- file1
    +-- file2
    +-- subdirectory/
```

A rule attached to the `allowed` directory can permit operations on
that directory and objects beneath it.

The process therefore gets a filesystem policy that looks conceptually
like:

```
allowed/
    READ
    WRITE
    CREATE
    REMOVE

denied/
    blocked
```

## no_new_privs

Before enforcing the Landlock ruleset, the program calls:

```
prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)
```

This prevents the process and its descendants from gaining additional
privileges through mechanisms such as executing a set-user-ID program.

Landlock requires this for the unprivileged use demonstrated in this
experiment.

The important sequence is:

```
create ruleset
      |
      v
add filesystem rules
      |
      v
set no_new_privs
      |
      v
restrict self
      |
      v
filesystem access is restricted
```

## Basic Read Experiment

The experiment created two directories:

```
allowed/
denied/
```

The process received a Landlock rule allowing access beneath:

```
allowed/
```

The process then attempted to read files from both directories.

The observed behavior was:

```
allowed/secret.txt
    READ -> succeeds

denied/secret.txt
    READ -> denied
```

This demonstrates that Landlock can restrict access based on the
filesystem object being accessed.

## Path Traversal

The experiment also attempted to access the denied directory through
a path containing:

```
../
```

For example:

```
allowed/../denied/secret.txt
```

The access was still denied.

This is important because the policy is not simply comparing strings
against filenames.

Landlock operates inside the kernel against the filesystem objects
being accessed.

A process cannot bypass the policy merely by constructing a different
relative path to the same object.

## Access Types Must Be Explicitly Handled

One of the most important observations from the experiment was that
Landlock only restricts access types included in the ruleset.

For example, the initial ruleset handled filesystem reads but did not
restrict writes.

As a result, an operation such as:

```
write to denied/file
```

could still succeed.

This is not a failure of Landlock.

It means the policy had not asked Landlock to restrict that particular
access type.

The mental model is:

```
access type included in ruleset
    -> Landlock can restrict it

access type not included in ruleset
    -> that access remains unrestricted
```

This is an important policy-design consideration.

## Adding Write Restrictions

The ruleset was then extended to include write access.

After adding:

```
LANDLOCK_ACCESS_FS_WRITE_FILE
```

the process attempted to write to files in both directories.

The results demonstrated:

```
allowed/file
    WRITE -> succeeds

denied/file
    WRITE -> denied
```

The same filesystem boundary now applied to both reads and writes.

## Creating Files

The experiment also added filesystem creation access types.

These include operations such as:

```
LANDLOCK_ACCESS_FS_MAKE_REG
LANDLOCK_ACCESS_FS_MAKE_DIR
```

The process then attempted to create filesystem objects beneath the
allowed and denied directories.

The observed behavior was:

```
allowed/
    create -> succeeds

denied/
    create -> denied
```

This demonstrates that Landlock can control not only access to existing
files, but also creation of new filesystem objects.

## Removing Files

The ruleset also included:

```
LANDLOCK_ACCESS_FS_REMOVE_FILE
LANDLOCK_ACCESS_FS_REMOVE_DIR
```

The process could therefore be restricted from deleting files or
directories beneath a denied subtree.

One early remove test returned:

```
ENOENT
No such file or directory
```

because the test file did not exist yet.

That result was a test setup issue rather than evidence that the
Landlock policy had failed.

After creating the appropriate test objects, the filesystem policy
could distinguish between permitted and denied removal operations.

## Landlock vs seccomp

The experiments with seccomp and Landlock demonstrate two different
security boundaries.

Seccomp operates at the syscall boundary:

```
process
   |
   v
syscall
   |
   v
seccomp policy
   |
   v
allow / deny / notify
```

Landlock operates at the filesystem-access boundary:

```
process
   |
   v
filesystem operation
   |
   v
Landlock policy
   |
   v
allowed filesystem object
or
denied filesystem object
```

A process might need the `openat()` syscall while still being forbidden
from opening a particular file.

Seccomp can control the syscall.

Landlock can control the filesystem access.

These mechanisms therefore solve different problems.

## AgentBox Security Model

Landlock is useful for an agent sandbox because an agent may need
filesystem access without being allowed unrestricted access to the
entire filesystem.

For example, an agent might be allowed to access:

```
/workspace/project/
```

while being prevented from accessing:

```
/etc/
/root/
/secrets/
```

A conceptual policy could be:

```
Agent
  |
  +-- /workspace/project
  |      READ
  |      WRITE
  |      CREATE
  |
  +-- /etc
  |      DENIED
  |
  +-- /root
  |      DENIED
  |
  +-- /secrets
         DENIED
```

This is different from a mount namespace.

A mount namespace controls what filesystem view the process receives.

Landlock controls what filesystem operations the process is allowed
to perform within that view.

Using both provides defense in depth.

## Current Sandbox Layers

At this point, AgentBox has several distinct isolation mechanisms:

```
Process
    basic process execution

PID namespace
    isolates process IDs and process visibility

User namespace
    isolates user and group identity

Mount namespace
    isolates mount configuration

Root filesystem
    provides an isolated filesystem tree

cgroups
    provide resource accounting and potential resource limits

capabilities
    control privileged kernel operations

seccomp
    restricts system calls

Landlock
    restricts filesystem operations

network namespace
    isolates the network stack

firewall
    restricts network-level connectivity

application proxy
    can enforce application-level HTTP policy
```

The mechanisms operate at different layers and are intended to
complement one another.

## Key Takeaways

The most important lessons from this experiment are:

1. Landlock is enforced by the kernel.
2. Landlock policies are expressed in terms of filesystem objects and
   access types.
3. A process can voluntarily restrict itself.
4. `no_new_privs` is part of the unprivileged enforcement model.
5. Path-beneath rules apply to filesystem hierarchies.
6. Different filesystem operations must be explicitly included in the
   ruleset.
7. Landlock and seccomp operate at different security boundaries.
8. Landlock is especially useful when an agent needs access to some
   files but not others.

For AgentBox, the broader lesson is that sandbox security should not
depend on a single mechanism.

```
namespaces
    isolate the environment

capabilities
    restrict privileged operations

seccomp
    restrict system calls

Landlock
    restrict filesystem access

network namespaces
    isolate networking

firewall
    restrict network connectivity

application proxy
    enforce application-level policy
```

Each layer addresses a different part of the sandbox.
