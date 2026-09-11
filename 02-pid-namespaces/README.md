# PID Namespaces

A PID namespace gives a process its own view of process IDs.

## Experiment

Compile:

    gcc -Wall -Wextra -o pidns pidns.c

Run:

    ./pidns

The program creates a child process using `clone()` with:

    CLONE_NEWPID

The child becomes PID 1 inside the new PID namespace.

## Key idea

A process can have different PIDs depending on which PID namespace is
looking at it.

PID namespaces are one of the primitives used by containers to isolate
process visibility.

## Important

This experiment only isolates PID visibility. It does not provide a
complete security sandbox.
