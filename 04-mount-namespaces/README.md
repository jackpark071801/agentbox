# Mount Namespaces

A mount namespace gives a process its own view of the system's mount
table.

## Inspect the current namespace

    readlink /proc/self/ns/mnt

## Create a new mount namespace

    unshare --mount bash

Inside the new shell:

    readlink /proc/self/ns/mnt

The namespace ID is different from the original shell.

## Mount isolation experiment

Inside the new namespace:

    mkdir -p /tmp/agentbox-mount-test
    mount -t tmpfs tmpfs /tmp/agentbox-mount-test
    touch /tmp/agentbox-mount-test/inside.txt

The tmpfs mount is visible from the new mount namespace.

After exiting back to the original namespace:

    mount | grep agentbox-mount-test

The mount is no longer visible.

## Key idea

A mount namespace does not create a new filesystem. It creates a different
view of the mount topology.

This is one of the primitives used by containers to give a process an
isolated filesystem view.
