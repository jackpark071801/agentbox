# Network Namespaces

This experiment explores Linux network namespaces and the primitives needed to give an isolated process controlled network access.

## Goal

Build a small network topology manually:

```text
Sandbox process
      |
      | network namespace
      |
10.200.0.2
      |
veth pair
      |
10.200.0.1
      |
Original network namespace
      |
     eth0
      |
Docker network
      |
   Internet
