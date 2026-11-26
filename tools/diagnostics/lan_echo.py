"""One-shot unicast UDP test on a LAN under your control."""
import argparse
import ipaddress
import secrets
import socket

PING = b"NOVA08?"
PONG = b"NOVA08!"
NETWORKS = tuple(map(ipaddress.ip_network, ("10.0.0.0/8", "172.16.0.0/12",
                                           "192.168.0.0/16", "127.0.0.0/8")))


def address(value):
    candidate = ipaddress.IPv4Address(value)
    if not any(candidate in network for network in NETWORKS):
        raise argparse.ArgumentTypeError("Use an explicit loopback or RFC1918 IPv4 address")
    return str(candidate)


def serve(sock):
    while True:
        packet, peer = sock.recvfrom(64)
        if packet.startswith(PING) and len(packet) == len(PING) + 16:
            sock.sendto(PONG + packet[len(PING):], peer)
            return


def request(sock, destination):
    nonce = secrets.token_bytes(16)
    with sock:
        sock.connect(destination)
        sock.send(PING + nonce)
        if sock.recv(64) != PONG + nonce:
            raise ValueError("Unexpected response")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=["serve", "ping"])
    parser.add_argument("address", type=address)
    parser.add_argument("port", type=int, nargs="?", default=38480)
    args = parser.parse_args()
    if not 1024 <= args.port <= 65535:
        parser.error("Use a port between 1024 and 65535")
    destination = (args.address, args.port)
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.settimeout(6)
            if args.mode == "serve":
                sock.bind(destination)
                serve(sock)
            else:
                request(sock, destination)
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")
    print("One UDP exchange completed")


if __name__ == "__main__":
    main()
