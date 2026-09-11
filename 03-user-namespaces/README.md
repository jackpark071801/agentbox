# User Namespaces

A Linux user namespace gives a process its own view of user and group IDs.

The important idea is that UID 0 inside a user namespace does not necessarily
correspond to host UID 0.

## Experiment

Create a new user namespace:

    unshare --user --map-root-user bash

Then inspect:

    id
    cat /proc/self/uid_map
    cat /proc/self/gid_map
    readlink /proc/self/ns/user

## Observation

Our Docker container already runs as root, so this experiment maps:

    namespace UID 0 -> container UID 0

The namespace itself is still distinct, as shown by its namespace inode:

    /proc/self/ns/user

In a real container runtime, an unprivileged host user can be mapped to
UID 0 inside the container.

## Important

This lab container runs with Docker `--privileged`, so it has far more
capabilities than a real sandbox should have.

User namespaces are only one part of a sandbox. They need to be combined
with mechanisms such as capability dropping, mount namespaces, seccomp,
Landlock, cgroups, and network isolation.
