# Application-Level Proxy

This experiment demonstrates how an application-layer proxy can enforce
HTTP policy, including for HTTPS traffic.

The network namespace experiment demonstrated Layer 3/4 policy:

```
IP address
protocol
port
connection state
```

That is useful, but it cannot inspect the HTTP request inside an HTTPS
connection.

This experiment moves the policy boundary up to Layer 7.

## Goal

Build a small HTTP proxy that can:

1. Receive HTTP proxy requests.
2. Forward allowed HTTP requests.
3. Handle HTTPS CONNECT.
4. Establish a bidirectional HTTPS tunnel.
5. Terminate client TLS.
6. Establish a separate TLS connection to the real server.
7. Decrypt and inspect HTTP requests.
8. Apply application-layer policy.
9. Block requests before they reach the upstream server.

## Initial HTTP Proxy

The first version of the proxy listened on:

```
0.0.0.0:8080
```

A request such as:

```
curl -v -x http://127.0.0.1:8080 http://example.com
```

produced a proxy request containing:

```
GET http://example.com/ HTTP/1.1
Host: example.com
```

This demonstrated that an HTTP proxy can inspect the HTTP request
directly.

The proxy was then extended to:

* parse the Host header
* allow specific hosts
* connect to the upstream server
* rewrite the proxy-form request target
* forward the request
* return the response

The initial application policy allowed:

```
example.com
```

and blocked other hosts with:

```
HTTP/1.1 403 Forbidden
```

## HTTPS and CONNECT

HTTPS through an HTTP proxy begins with a CONNECT request.

For example:

```
CONNECT example.com:443 HTTP/1.1
Host: example.com:443
```

The proxy establishes a TCP connection to the requested destination
and responds:

```
HTTP/1.1 200 Connection Established
```

At that point, the client normally starts a TLS handshake through
the connection.

## Bidirectional CONNECT Tunnel

The first CONNECT implementation only relayed traffic from the client
to the upstream server.

TLS therefore stalled because the server's response could not travel
back to the client.

The relay was changed to copy data in both directions:

```
client -> proxy -> upstream
client <- proxy <- upstream
```

After this change:

```
curl -v -x http://127.0.0.1:8080 https://example.com
```

completed a normal TLS handshake and HTTP request.

At this stage the proxy could enforce destination-level HTTPS policy
but could not see the HTTP request inside TLS.

## TLS Interception

The next step was to terminate TLS at the proxy instead of blindly
tunneling encrypted bytes.

A local AgentBox certificate authority was created:

```
certs/agentbox-ca.key
certs/agentbox-ca.crt
```

A certificate for example.com was then generated and signed by the
AgentBox CA:

```
certs/example.com.key
certs/example.com.crt
```

The certificate included:

```
subjectAltName=DNS:example.com
```

It was verified with:

```
openssl verify \
  -CAfile certs/agentbox-ca.crt \
  certs/example.com.crt
```

which returned:

```
example.com.crt: OK
```

## Two TLS Connections

The proxy now establishes two independent TLS sessions:

```
curl
  |
  | TLS connection #1
  v
AgentBox proxy
  |
  | TLS connection #2
  v
example.com
```

The proxy uses Python's ssl module to:

* terminate TLS from the client
* present the AgentBox example.com certificate
* validate the real server certificate
* establish TLS to the real server

When curl does not trust the AgentBox CA, certificate verification
fails.

Using:

```
--cacert certs/agentbox-ca.crt
```

allows curl to trust the lab CA.

This proves that the proxy is actually terminating TLS rather than
merely tunneling encrypted bytes.

## Decrypted HTTPS Inspection

After TLS termination, the proxy can read the plaintext HTTP request
from the client-side TLS socket.

For example:

```
GET / HTTP/1.1
Host: example.com
User-Agent: curl/8.5.0
Accept: */*
```

The proxy then forwards the request over the separate upstream TLS
connection.

This is the critical Layer 7 transition:

```
Encrypted HTTPS
      |
      | TLS termination
      v
  Plain HTTP
      |
      | inspect
      v
 Policy decision
      |
      | TLS
      v
 Upstream server
```

Before TLS interception, the proxy could only see information such as
the destination host and port.

After TLS interception, the proxy can inspect the actual HTTP request.

## Application-Layer Policy

The proxy parses the decrypted HTTP request into:

* method
* target
* HTTP version
* headers
* host

The policy is implemented separately in:

```
check_http_policy()
```

The current policy allows:

```
Host: example.com
Method: GET
Path: anything except /admin
```

It denies:

```
Host != example.com
Method != GET
Path == /admin
```

### Allowed Request

Testing:

```
curl -v \
  --cacert certs/agentbox-ca.crt \
  -x http://127.0.0.1:8080 \
  https://example.com/
```

produced:

```
ALLOWED HTTP REQUEST: GET /
```

The proxy forwarded the request and received the upstream response.

### Blocked Path

Testing:

```
curl -v \
  --cacert certs/agentbox-ca.crt \
  -x http://127.0.0.1:8080 \
  https://example.com/admin
```

produced:

```
BLOCKED HTTP REQUEST: GET /admin
Reason: path not allowed
```

The request was rejected locally with:

```
HTTP/1.1 403 Forbidden
```

It was not forwarded to the upstream server.

### Blocked Method

Testing:

```
curl -v \
  --cacert certs/agentbox-ca.crt \
  -x http://127.0.0.1:8080 \
  -X POST \
  https://example.com/
```

produced:

```
BLOCKED HTTP REQUEST: POST /
Reason: method not allowed
```

Again, the request was rejected before being sent upstream.

## L4 vs L7 Policy

The network namespace experiment demonstrated policy such as:

```
sandbox -> 1.1.1.1:443
```

This is Layer 3/4 policy.

The application proxy can express:

```
example.com
GET
/
```

or:

```
example.com
POST
/
```

or:

```
example.com
GET
/admin
```

This is Layer 7 policy.

The distinction is:

```
iptables
    IP / protocol / port / connection state

application proxy
    host / method / path / headers / body
```

For HTTPS, the application proxy needs TLS interception before it can
inspect the HTTP contents.

## Current Limitation: Request Bodies

The current experiment reads the HTTP headers before making the policy
decision.

A production-quality proxy must also correctly handle request bodies,
including:

* Content-Length
* Transfer-Encoding: chunked
* larger bodies
* streaming requests
* connection reuse

This is an important limitation of the current experiment.

## HTTP Protocol Limitations

This proxy is intentionally minimal.

A production proxy would need robust handling for things such as:

* persistent connections
* HTTP/1.1 framing
* chunked encoding
* request bodies
* response bodies
* connection shutdown
* HTTP/2
* HTTP/3
* certificate generation for arbitrary hosts
* certificate caching
* upstream authentication
* timeouts
* concurrency
* malformed requests

The purpose of this experiment is to expose the application-layer
security boundary rather than build a production HTTP proxy.

## AgentBox Relevance

This experiment models an application-layer enforcement point for a
sandbox that needs to control an agent's external API access.

For example, a sandbox could potentially enforce policies such as:

```
Agent
  |
  +-- api.example.com
  |      GET /models
  |      GET /status
  |
  +-- blocked.example.com
         denied
```

More sophisticated policy could eventually consider:

* hostname
* HTTP method
* URL path
* headers
* request body
* identity of the sandbox or agent
* API credentials
* rate limits
* audit logging

The major architectural lesson is that network isolation and
application policy are complementary:

```
Network namespace
    controls the network environment

Firewall
    controls network-level reachability

Application proxy
    controls application-level requests
```

## Security Note

The CA private key in:

```
certs/agentbox-ca.key
```

exists only for this disposable learning experiment.

A real system would protect its CA key carefully and would need a much
more complete TLS interception design.

The Docker container is privileged and this lab is not a production
security boundary.
