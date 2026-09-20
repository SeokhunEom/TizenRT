#!/usr/bin/env python3
"""Protocol operations and validation must survive Python -O/-OO."""
import importlib.util
import io
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('fault_message', Path(__file__).with_name('fault-message.py'))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class Socket:
    def __init__(self, data):
        self.data = bytearray(data)
        self.sent = []

    def recv(self, size):
        data = self.data[:1]  # TCP may fragment the two checksum bytes.
        del self.data[:1]
        return bytes(data)

    def sendall(self, data):
        self.sent.append(data)


def packet(text):
    raw = text.encode()
    return b'$' + raw + b'#' + f'{sum(raw) & 255:02x}'.encode()


class ProtocolTests(unittest.TestCase):
    def remote(self, data):
        connection = Socket(data)
        return runner.Remote(connection, io.StringIO()), connection

    def test_breakpoints_and_ack_are_always_executed(self):
        remote, connection = self.remote(b'+' + packet('OK') + b'+' + packet('OK'))
        remote.breakpoint(0x1234)
        remote.breakpoint(0x1234, False)
        self.assertEqual(connection.sent, [packet('Z1,1234,2'), b'+', packet('z1,1234,2'), b'+'])
        self.assertFalse(connection.data)

    def test_breakpoint_failure_is_fatal(self):
        remote, _ = self.remote(b'+' + packet('E01'))
        with self.assertRaises(RuntimeError):
            remote.breakpoint(0x1234)

    def test_bad_ack_is_fatal(self):
        remote, _ = self.remote(b'-')
        with self.assertRaises(RuntimeError):
            remote.send('c')

    def test_bad_checksum_is_fatal(self):
        remote, _ = self.remote(b'$OK#00')
        with self.assertRaises(RuntimeError):
            remote.packet()

    def test_truncated_checksum_is_fatal(self):
        remote, _ = self.remote(packet('OK')[:-1])
        with self.assertRaises(EOFError):
            remote.packet()

    def test_short_memory_reply_is_fatal(self):
        remote, _ = self.remote(b'+' + packet('aa'))
        with self.assertRaises(RuntimeError):
            remote.memory(0x1000, 4)


if __name__ == '__main__':
    unittest.main()
