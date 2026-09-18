#!/usr/bin/env python3
"""Verify QEMU network APIs and binary TCP/UDP transfers in an isolated container."""
import argparse
from pathlib import Path
import re
import socket
import sys
import threading

from qemu_common import QemuSession, run_container

SIZES = (1, 3, 64, 513, 1024, 1484)
TOTAL = sum(SIZES)


def payload(index, length):
    return bytes((i * 37 + index * 13) & 255 for i in range(length))


def receive_exact(sock, length):
    data = bytearray()
    while len(data) < length:
        block = sock.recv(length - len(data))
        if not block:
            raise RuntimeError('TCP closed before complete payload')
        data.extend(block)
    return bytes(data)


def peer_done(qemu, protocol, role):
    return qemu.wait(('NETPEER PASS ' + protocol + ' ' + role +
                      ' rounds=6 bytes=' + str(TOTAL) + r'\r?\n').encode())


def start_echo(protocol, corrupt=False):
    """Return a listening socket, worker, and its independently checked results."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM if protocol == 'tcp' else socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(('127.0.0.1', 40011 if protocol == 'tcp' else 40012))
    sock.settimeout(60)
    if protocol == 'tcp':
        sock.listen(1)
    report = {'status': 'fail', 'rounds': 0, 'bytes': 0}

    def echo():
        connection = None
        try:
            if protocol == 'tcp':
                connection, _ = sock.accept()
                connection.settimeout(60)
            for index, length in enumerate(SIZES):
                expected = payload(index, length)
                if connection is not None:
                    data = receive_exact(connection, length)
                else:
                    data, address = sock.recvfrom(65535)
                if data != expected:
                    raise RuntimeError('Host received corrupt ' + protocol + ' payload')
                response = data
                if corrupt and index == 0:
                    response = bytes([data[0] ^ 1]) + data[1:]
                if connection is not None:
                    connection.sendall(response)
                else:
                    sock.sendto(response, address)
                report['rounds'] += 1
                report['bytes'] += length
                if corrupt:
                    break
            report['status'] = 'pass'
        except Exception as exc:
            report['error'] = str(exc)
        finally:
            if connection is not None:
                connection.close()
            sock.close()

    worker = threading.Thread(target=echo)
    worker.daemon = True
    worker.start()
    return sock, worker, report


def host_client(protocol):
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM if protocol == 'tcp' else socket.SOCK_DGRAM) as sock:
        sock.settimeout(60)
        sock.connect(('127.0.0.1', 40001 if protocol == 'tcp' else 40002))
        for index, length in enumerate(SIZES):
            data = payload(index, length)
            if protocol == 'tcp':
                sock.sendall(data)
                received = receive_exact(sock, length)
            else:
                if sock.send(data) != length:
                    raise RuntimeError('Short host UDP send')
                received = sock.recv(65535)
            if received != data:
                raise RuntimeError('Host received corrupt echo: ' + protocol)
    return {'status': 'pass', 'rounds': len(SIZES), 'bytes': TOTAL}


def check_network(qemu, result, cycles=2, corrupt_reply=False):
    """Run binary network checks in the caller-owned, already booted QEMU."""
    peers = []
    try:
        for protocol, port in (('tcp', 15011), ('udp', 15012)):
            qemu.send('network_peer ' + protocol + ' server 127.0.0.1 ' + str(port))
            qemu.wait(('NETPEER READY ' + protocol + ' server').encode())
            qemu.send('network_peer ' + protocol + ' client 127.0.0.1 ' + str(port))
            roles = []
            for _ in range(2):
                text = qemu.wait(('NETPEER PASS ' + protocol + r' (client|server) rounds=6 bytes=' + str(TOTAL) + r'\r?\n').encode())
                roles.append(re.search(r'NETPEER PASS \w+ (client|server)', text).group(1))
            if set(roles) != {'client', 'server'}:
                raise RuntimeError('Incomplete loopback peer completion')
            result['checks'].append({'name': 'loopback-' + protocol, 'status': 'pass', 'bytes_each_way': TOTAL})
            qemu.shell('')

        for cycle in range(cycles):
            if cycle:
                qemu.shell('ifdown eth0', ['OK'])
                result['after_ifdown'] = qemu.shell('ifconfig eth0')
                if not re.search(r'eth0[^\r\n]*<DOWN(?:,RUNNING)?>', result['after_ifdown']):
                    raise RuntimeError('eth0 did not go down')
            qemu.shell('ifup eth0', ['OK'])
            qemu.shell('ifconfig eth0 dhcp', ['get IP address 10.0.2.15'])
            result['address_cycle_' + str(cycle)] = qemu.shell('ifconfig eth0', ['10.0.2.15', '<UP,RUNNING>'])
            if not re.search(r'eth0[^\r\n]*<UP,RUNNING>', result['address_cycle_' + str(cycle)]):
                raise RuntimeError('eth0 did not come up')
            qemu.send('ping 10.0.2.2')
            ping = qemu.wait(rb'3 packets transmitted, 3 received, 0\.000000% packet loss')
            result['checks'].append({'name': 'dhcp-ping-' + str(cycle), 'status': 'pass', 'output': ping})
            qemu.shell('')
            for protocol in ('tcp', 'udp'):
                peer_sock, worker, report = start_echo(protocol, corrupt_reply)
                peers.append((peer_sock, worker))
                port = 40011 if protocol == 'tcp' else 40012
                qemu.send('network_peer ' + protocol + ' client 10.0.2.2 ' + str(port))
                peer_done(qemu, protocol, 'client')
                worker.join(2)
                if worker.is_alive() or report['status'] != 'pass' or report['bytes'] != TOTAL:
                    raise RuntimeError('Host echo incomplete: ' + repr(report))
                result['checks'].append(dict(report, name='guest-to-host-' + protocol + '-' + str(cycle)))
                qemu.shell('')
                port = 15001 if protocol == 'tcp' else 15002
                qemu.send('network_peer ' + protocol + ' server 0.0.0.0 ' + str(port))
                qemu.wait(('NETPEER READY ' + protocol + ' server').encode())
                host_report = host_client(protocol)
                peer_done(qemu, protocol, 'server')
                result['checks'].append(dict(host_report, name='host-to-guest-' + protocol + '-' + str(cycle)))
                qemu.shell('')
    finally:
        for sock, worker in peers:
            sock.close()
            worker.join(0.2)


def inside(args):
    out = args.output
    result = {'status': 'fail', 'checks': [], 'peer_sizes': SIZES}
    qemu = QemuSession(out)
    try:
        qemu.start(network=True)
        result['qemu_command'] = qemu.command
        result['inventory'] = {cmd: qemu.shell(cmd) for cmd in ('help', 'ifconfig', 'free', 'mount')}
        if 'network_peer' not in result['inventory']['help']:
            raise RuntimeError('network_peer command is absent')
        qemu.shell('echo QEMU-network-guard > /mnt/qemu-network-guard')
        if 'QEMU-network-guard' not in qemu.shell('cat /mnt/qemu-network-guard').splitlines():
            raise RuntimeError('Could not create storage guard')
        check_network(qemu, result, corrupt_reply=args.corrupt_reply)
        result['post'] = {cmd: qemu.shell(cmd) for cmd in ('ps', 'free', 'ifconfig', 'mount')}
        guard = qemu.shell('cat /mnt/qemu-network-guard')
        if 'QEMU-network-guard' not in guard.splitlines():
            raise RuntimeError('Network checks lost storage guard')
        result['storage_guard_survived'] = True
        qemu.shell('rm /mnt/qemu-network-guard')
        result['status'] = 'pass'
    except Exception as exc:
        result['error'] = str(exc)
    finally:
        qemu.finish(result)
    return 0 if result['status'] == 'pass' else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--image', default='tizenrt/qemu-build-test:2.12.0-16m')
    parser.add_argument('--inside', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('--corrupt-reply', action='store_true', help='Inject a bad host echo; verification must fail')
    args = parser.parse_args()
    if args.inside:
        return inside(args)
    if args.root is None:
        parser.error('--root is required')
    extra = ['--corrupt-reply'] if args.corrupt_reply else []
    return run_container(args.root, args.output, args.image, 'network-smoke.py', extra, 360,
                         {'corrupt_reply': args.corrupt_reply})


if __name__ == '__main__':
    sys.exit(main())
