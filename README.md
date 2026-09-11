# AgentBox

A hands-on project for learning Linux sandboxing by building the
primitives behind an agent sandbox from scratch.

The goal is to understand what Linux containers and sandboxing systems
actually do at the kernel boundary, rather than treating containers as
a black box.

## Learning path

### Process isolation

1. Processes
2. PID namespaces
3. User namespaces
4. Mount namespaces
5. Isolated root filesystem

### Resource and privilege isolation

6. cgroups
7. Linux capabilities
8. seccomp
9. Landlock

### Network isolation

10. Network namespaces
11. veth pairs
12. Internet connectivity and NAT
13. Network policy

### Application-level policy

14. HTTP policy proxy
15. HTTPS interception
16. Credential proxy

### Kernel observability and enforcement

17. eBPF process tracing
18. eBPF network tracing

### AgentBox

19. Sandbox supervisor
20. Agent execution lifecycle
21. Adversarial sandbox tests

## Method

Each milestone follows the same pattern:

1. Understand the Linux primitive
2. Observe it from the shell
3. Build a tiny experiment
4. Break it
5. Understand the failure
6. Implement the primitive in code
7. Test it
8. Commit the result

## Environment

The experiments run inside a disposable Ubuntu userspace in Docker.

The Docker container is privileged because several early experiments
require capabilities that are normally restricted inside containers.

This is a learning environment, not a production security boundary.
