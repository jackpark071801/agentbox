# Processes

This experiment explores Linux processes, PIDs, and parent/child
relationships.

## Experiment

Compile:

    gcc -Wall -Wextra -o hello hello.c

Run:

    ./hello

Inspect the process ID and parent process ID.

## Key idea

A process is a running instance of a program.

Every process has a PID and normally has a parent process.

This is the foundation for understanding PID namespaces, process
isolation, and sandbox lifecycle management.
