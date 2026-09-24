import socket
from urllib.parse import urlsplit

LISTEN_HOST = "0.0.0.0"
LISTEN_PORT = 8080

ALLOWED_HOSTS = {
    "example.com",
}


def recv_until_headers_complete(sock):
    data = b""

    while b"\r\n\r\n" not in data:
        chunk = sock.recv(4096)

        if not chunk:
            break

        data += chunk

        if len(data) > 65536:
            raise ValueError("Request headers too large")

    return data


def parse_request(request):
    text = request.decode("utf-8", errors="replace")
    lines = text.split("\r\n")

    request_line = lines[0]
    method, target, version = request_line.split(" ", 2)

    headers = {}

    for line in lines[1:]:
        if not line:
            break

        name, value = line.split(":", 1)
        headers[name.lower()] = value.strip()

    host = headers.get("host")

    if not host:
        raise ValueError("Missing Host header")

    return method, target, version, headers, host


def make_origin_request(request, target):
    text = request.decode("utf-8", errors="replace")

    lines = text.split("\r\n")

    method, _, version = lines[0].split(" ", 2)

    parsed = urlsplit(target)

    path = parsed.path or "/"

    if parsed.query:
        path += "?" + parsed.query

    lines[0] = f"{method} {path} {version}"

    return "\r\n".join(lines).encode()


def recv_response(sock):
    data = b""

    while b"\r\n\r\n" not in data:
        chunk = sock.recv(4096)

        if not chunk:
            return data

        data += chunk

    header_end = data.index(b"\r\n\r\n") + 4

    headers = data[:header_end]
    body = data[header_end:]

    header_text = headers.decode("iso-8859-1")

    content_length = None
    chunked = False

    for line in header_text.split("\r\n"):
        if ":" not in line:
            continue

        name, value = line.split(":", 1)

        if name.lower() == "content-length":
            content_length = int(value.strip())

        if name.lower() == "transfer-encoding":
            if "chunked" in value.lower():
                chunked = True

    if content_length is not None:
        while len(body) < content_length:
            chunk = sock.recv(4096)

            if not chunk:
                break

            body += chunk

        return headers + body[:content_length]

    if chunked:
        while not body.endswith(b"\r\n0\r\n\r\n"):
            chunk = sock.recv(4096)

            if not chunk:
                break

            body += chunk

        return headers + body

    # No Content-Length and not chunked:
    # fall back to reading until the server closes the connection.
    while True:
        chunk = sock.recv(4096)

        if not chunk:
            break

        body += chunk

    return headers + body


def send_error(client, status, message):
    response = (
        f"HTTP/1.1 {status} {message}\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n"
    ).encode()

    client.sendall(response)

def relay(client, upstream):
    import select

    sockets = [client, upstream]

    while True:
        readable, _, _ = select.select(sockets, [], [])

        for sock in readable:
            data = sock.recv(4096)

            if not data:
                return

            if sock is client:
                upstream.sendall(data)
            else:
                client.sendall(data)


def handle_connect(client, host):
    # CONNECT gives us "example.com:443"
    if ":" not in host:
        send_error(client, 400, "Bad Request")
        return

    hostname, port_text = host.rsplit(":", 1)
    port = int(port_text)

    if hostname not in ALLOWED_HOSTS:
        print(f"BLOCKED CONNECT: {hostname}")
        send_error(client, 403, "Forbidden")
        return

    print(f"ALLOWED CONNECT: {hostname}:{port}")

    upstream = socket.create_connection(
        (hostname, port),
        timeout=5,
    )

    try:
        client.sendall(
            b"HTTP/1.1 200 Connection Established\r\n"
            b"Proxy-Agent: AgentBox\r\n"
            b"\r\n"
        )

        print("CONNECT tunnel established")

        relay(client, upstream)

    finally:
        upstream.close()


def handle_client(client, addr):
    print(f"\nConnection from {addr}")

    request = recv_until_headers_complete(client)

    print("----- REQUEST -----")
    print(request.decode("utf-8", errors="replace"))
    print("-------------------")

    method, target, version, headers, host = parse_request(request)

    print(f"Host: {host}")
    print(f"Method: {method}")
    print(f"Target: {target}")

    if method == "CONNECT":
        handle_connect(client, host)
        return

    if host not in ALLOWED_HOSTS:
        print(f"BLOCKED: {host}")
        send_error(client, 403, "Forbidden")
        return

    print(f"ALLOWED: {host}")

    origin_request = make_origin_request(request, target)

    print("----- UPSTREAM REQUEST -----")
    print(origin_request.decode("utf-8", errors="replace"))
    print("----------------------------")

    upstream = socket.create_connection((host, 80), timeout=5)

    try:
        upstream.sendall(origin_request)

        response = recv_response(upstream)

        print(f"Received {len(response)} response bytes")

        client.sendall(response)

    finally:
        upstream.close()


server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
server.bind((LISTEN_HOST, LISTEN_PORT))
server.listen()

print(f"Proxy listening on {LISTEN_HOST}:{LISTEN_PORT}")

while True:
    client, addr = server.accept()

    try:
        handle_client(client, addr)

    except Exception as e:
        print(f"ERROR: {e}")

    finally:
        client.close()
