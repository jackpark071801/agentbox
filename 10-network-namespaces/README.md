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
```

The experiment demonstrates:

* network namespace isolation
* loopback
* virtual Ethernet pairs
* moving an interface into another namespace
* routing
* IP forwarding
* NAT
* firewall policy
* connection tracking
* DNS
* destination-specific network policy

## Baseline Network

The original container network namespace contained:

```text
lo
eth0
```

The container's Ethernet interface looked like:

```text
eth0@if17
```

The `@if17` notation indicates that the interface is one end of a virtual Ethernet pair.

The container had an address in the Docker network:

```text
172.17.0.2/16
```

and a default route:

```text
default via 172.17.0.1 dev eth0
```

The basic commands used to inspect this were:

```bash
ip link
ip addr
ip route
```

These commands operate on the network namespace of the current process.

## Creating an Empty Network Namespace

A new network namespace was created with:

```bash
unshare --net bash
```

Inside the new namespace, the normal `eth0` interface was gone.

The namespace initially contained only the loopback interface plus some kernel-created tunnel devices.

There was also no default route.

For example:

```bash
ip addr show eth0
```

returned:

```text
Device "eth0" does not exist.
```

And:

```bash
ip route
```

showed no usable routes.

This demonstrates that a network namespace creates an independent network stack.

It does not automatically inherit the interfaces or routes from the original namespace.

## Loopback

The loopback interface exists separately inside each network namespace.

It was brought up with:

```bash
ip link set lo up
```

After that:

```bash
ping -c 1 127.0.0.1
```

succeeded.

This demonstrates that the isolated namespace has its own loopback device and its own local network stack.

However, loopback connectivity does not provide access to the external network.

## No Automatic Connectivity

Inside the empty network namespace:

```bash
ping -c 1 172.17.0.1
```

failed with:

```text
Network is unreachable
```

This happened because the new namespace had no interface connected to the Docker network and no route to `172.17.0.0/16`.

The important lesson is:

```text
network namespace
        !=
network connectivity
```

A namespace provides isolation.

Connectivity must be constructed explicitly.

## Keeping a Namespace Alive

To build a network topology, the namespace needs to remain alive.

The experiment used:

```bash
unshare --net --fork bash -c 'ip link set lo up; exec sleep 100000' &
```

This created a process whose network namespace could be inspected.

There were two relevant PIDs:

```text
unshare process
      |
      +-- process inside network namespace
```

The PID of the process inside the namespace was used with:

```bash
nsenter -t <PID> -n
```

The namespace itself can be identified with:

```bash
readlink /proc/<PID>/ns/net
```

The original namespace and sandbox namespace had different namespace inodes.

For example:

```text
net:[4026532807]
```

versus:

```text
net:[4026532548]
```

This proves that the process is operating inside a different network namespace.

## Virtual Ethernet Pair

A veth pair was created with:

```bash
ip link add veth-host type veth peer name veth-sandbox
```

A veth pair behaves like a virtual Ethernet cable.

Conceptually:

```text
veth-host <----------------> veth-sandbox
```

Each endpoint exists in a network namespace.

The sandbox endpoint was moved into the sandbox network namespace:

```bash
ip link set veth-sandbox netns <PID>
```

The host-side interface remained in the original namespace.

The two namespaces were then given addresses:

```text
veth-host
    10.200.0.1/24

veth-sandbox
    10.200.0.2/24
```

Both interfaces were brought up.

The resulting topology was:

```text
Original namespace              Sandbox namespace

veth-host                       veth-sandbox
10.200.0.1  <---------------->  10.200.0.2
```

The sandbox could then ping the host-side veth:

```bash
nsenter -t <PID> -n ping -c 3 10.200.0.1
```

This demonstrates that the veth pair provides actual Layer 2 connectivity between the two network namespaces.

## Routing

The sandbox initially knew only about its directly connected `10.200.0.0/24` network.

A route to the Docker network was added:

```bash
ip route add 172.17.0.0/16 \
  via 10.200.0.1 \
  dev veth-sandbox
```

A default route was then added:

```bash
ip route add default \
  via 10.200.0.1 \
  dev veth-sandbox
```

The resulting sandbox routing table contained conceptually:

```text
10.200.0.0/24
    directly connected

172.17.0.0/16
    via 10.200.0.1

default
    via 10.200.0.1
```

This demonstrates that routing tables are associated with network namespaces.

A route existing in the original namespace does not automatically exist in the sandbox namespace.

## IP Forwarding

At this point, packets could travel:

```text
sandbox
   |
   v
veth pair
   |
   v
original namespace
   |
   v
Docker eth0
```

But the original namespace needed to forward packets between its interfaces.

IP forwarding was enabled with:

```bash
sysctl -w net.ipv4.ip_forward=1
```

This changes the Linux kernel's IPv4 forwarding behavior.

Conceptually:

```text
veth-host
    |
    | forward
    v
  eth0
```

Without forwarding, the original namespace behaves like an endpoint rather than a router for these packets.

## NAT

The sandbox uses the private address:

```text
10.200.0.2
```

The Docker network does not automatically know how to route replies back to this private subnet.

Network address translation was therefore added:

```bash
iptables -t nat -A POSTROUTING \
  -s 10.200.0.0/24 \
  -o eth0 \
  -j MASQUERADE
```

MASQUERADE rewrites the source address of packets leaving through `eth0`.

Conceptually:

```text
Before NAT:

10.200.0.2 -> Internet

After NAT:

172.17.0.2 -> Internet
```

The return traffic is then translated back to the sandbox connection.

The NAT rule's packet and byte counters increased when sandbox traffic passed through it.

This demonstrates that the sandbox can reach an external network when routing, forwarding, and NAT are configured.

## Internet Connectivity

After routing, forwarding, and NAT were configured, the sandbox could reach external IP addresses.

For example:

```bash
nsenter -t <PID> -n ping -c 1 1.1.1.1
```

succeeded.

The path was effectively:

```text
Sandbox
  |
  | 10.200.0.2
  v
veth pair
  |
  | 10.200.0.1
  v
Original namespace
  |
  | NAT
  v
Docker network
  |
  v
Internet
```

The sandbox therefore has controlled connectivity rather than direct access to the Docker network interface.

## Firewall Policy

Once connectivity existed, the next question was:

```text
Can we control which traffic is allowed?
```

A blanket forwarding rule was first tested:

```bash
iptables -A FORWARD \
  -s 10.200.0.0/24 \
  -j DROP
```

Sandbox traffic then stopped being forwarded.

Packet counters increased on the rule, demonstrating that packets were actually reaching the forwarding chain.

The rule was then removed.

This established the basic enforcement model:

```text
sandbox packet
      |
      v
   FORWARD
      |
  policy rule
      |
  allow / drop
```

## Layer 4 Policy

iptables can express policies based on information such as:

* source IP
* destination IP
* protocol
* source port
* destination port
* connection state

For example, TCP port 443 traffic was blocked with a rule matching:

```text
-p tcp
--dport 443
```

After adding the rule, a TCP connection attempt to an HTTPS service timed out and the firewall rule's counter increased.

This demonstrates Layer 3/4 network policy.

However, the firewall still cannot see the HTTP request itself.

It can distinguish:

```text
10.200.0.2 -> 1.1.1.1:443
```

but not:

```http
GET /models HTTP/1.1
Host: api.example.com
```

That distinction becomes important for the application proxy experiment.

## Connection Tracking

The firewall was also tested with connection tracking.

An established/related rule was added:

```bash
iptables -A FORWARD \
  -m conntrack \
  --ctstate ESTABLISHED,RELATED \
  -j ACCEPT
```

This allows packets belonging to connections that have already been classified as established or related.

The experiment also logged new sandbox connections:

```bash
iptables -A FORWARD \
  -s 10.200.0.0/24 \
  -m conntrack \
  --ctstate NEW \
  -j LOG \
  --log-prefix "AGENTBOX-NEW: "
```

The important distinction is:

```text
NEW
    first packet establishing a connection

ESTABLISHED
    traffic belonging to an existing connection

RELATED
    traffic associated with an existing connection
```

The LOG target is non-terminal.

Logging a packet does not itself allow or deny it. Later rules still process the packet.

## Default-Deny Policy

The forwarding policy was changed to:

```bash
iptables -P FORWARD DROP
```

This creates a default-deny posture for forwarded traffic.

Specific traffic can then be allowed with explicit rules.

The experiment allowed:

```text
TCP/80
    anywhere
```

and later allowed:

```text
TCP/443
    only to 1.1.1.1
```

Other forwarded traffic remained blocked.

Conceptually:

```text
sandbox -> anywhere:80
    ALLOW

sandbox -> 1.1.1.1:443
    ALLOW

sandbox -> other destinations:443
    DROP

sandbox -> other ports
    DROP
```

This is a useful basic network policy model for a sandbox.

## Destination-Specific Policy

A generic TCP/443 allow rule would permit HTTPS to any destination.

The experiment replaced that broad rule with a destination-specific rule:

```text
source:
    10.200.0.0/24

destination:
    1.1.1.1

protocol:
    TCP

destination port:
    443
```

After removing the broad HTTPS rule:

```text
1.1.1.1:443
    succeeded

8.8.8.8:443
    timed out
```

This demonstrates that firewall policy can restrict not only ports, but also destination IP addresses.

## DNS

IP connectivity and DNS are separate concerns.

The sandbox's `/etc/resolv.conf` contained:

```text
nameserver 192.168.65.7
```

Initially, DNS queries from the sandbox timed out.

Packet capture on the veth showed DNS queries leaving the sandbox:

```text
10.200.0.2:<port> -> 192.168.65.7:53
```

The lack of a response was caused by the firewall's default-deny forwarding policy.

A narrow DNS rule was then added:

```bash
iptables -I FORWARD \
  -s 10.200.0.0/24 \
  -d 192.168.65.7 \
  -p udp \
  --dport 53 \
  -m conntrack \
  --ctstate NEW \
  -j ACCEPT
```

After that, a DNS query succeeded:

```bash
dig @192.168.65.7 example.com
```

This demonstrates that a sandbox can be given DNS access independently of other network permissions.

## Packet Capture

tcpdump was used to observe traffic on the host-side veth.

For example, while DNS was blocked, packet capture showed:

```text
10.200.0.2 -> 192.168.65.7:53
```

with repeated queries and no successful response.

This is useful because it separates different failure modes.

For example:

```text
Application
    |
    v
DNS query generated
    |
    v
Packet leaves sandbox
    |
    v
Firewall
    |
    v
packet dropped
```

Without packet capture, an application timeout alone would not show where the failure occurred.

## Final Firewall Model

The final experimental policy was conceptually:

```text
ESTABLISHED,RELATED
    ALLOW

Sandbox -> DNS server:53/UDP
    ALLOW

Sandbox -> anywhere:80/TCP
    ALLOW

Sandbox -> 1.1.1.1:443/TCP
    ALLOW

Everything else
    DROP
```

The forwarding chain therefore became a basic network policy enforcement point for the sandbox.

## L3/L4 vs Application-Layer Policy

This experiment demonstrates the limits of network-layer policy.

The firewall can answer questions such as:

```text
Is this packet from the sandbox?

Is it TCP?

Is the destination port 443?

Is the destination IP 1.1.1.1?

Is this connection NEW or ESTABLISHED?
```

It cannot answer questions such as:

```text
Is this an HTTP GET?

Is the Host header api.example.com?

Is the path /models?

Does the request contain a particular API operation?
```

Those questions require an application-aware enforcement point.

That is the reason for the application proxy experiment.

## AgentBox Relevance

A useful sandbox network architecture is:

```text
Agent
  |
  v
Network namespace
  |
  v
veth pair
  |
  v
Firewall / router
  |
  +---- allowed network traffic
  |
  v
Application proxy
  |
  v
External APIs
```

The network namespace provides isolation.

The veth pair provides a controlled connection between namespaces.

Routing determines where traffic can go.

NAT provides external connectivity.

The firewall provides Layer 3/4 policy.

The application proxy can then enforce Layer 7 policy.

These are complementary controls rather than interchangeable ones.

## Key Takeaways

The most important lessons from this experiment are:

1. A network namespace creates an independent network stack.
2. A new network namespace does not automatically have external connectivity.
3. A veth pair acts like a virtual Ethernet cable between namespaces.
4. Routing tables are namespace-specific.
5. IP forwarding allows the original namespace to act as a router.
6. NAT allows private sandbox addresses to reach external networks.
7. iptables can enforce Layer 3/4 policy.
8. Connection tracking distinguishes new and established traffic.
9. DNS access must be permitted separately when using a default-deny firewall.
10. Packet capture helps identify where network failures occur.
11. Network-layer policy cannot inspect encrypted HTTP contents.
12. Application-layer policy requires a higher-level enforcement point.

For AgentBox, the core architecture is:

```text
namespace
    +
routing
    +
NAT
    +
firewall
    +
application proxy
```

The network namespace controls the environment.

The firewall controls network reachability.

The application proxy controls application-level requests.
