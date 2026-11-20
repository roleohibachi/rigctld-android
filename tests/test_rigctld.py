"""Smoke-test the embedded server against the pinned Hamlib rigctl client."""

import socket
import subprocess
import sys
import time
import unittest
from pathlib import Path
from select import select


SERVER, CLIENT = map(lambda name: Path(name).resolve(), sys.argv[1:3])
del sys.argv[1:3]


class RigctldTests(unittest.TestCase):
    def setUp(self):
        with socket.socket() as available:
            available.bind(("127.0.0.1", 0))
            self.port = available.getsockname()[1]
        self.server = self.start_server()
        self.addCleanup(self.stop_server)
        self.wait_for_listener()

    def wait_for_listener(self):
        self.assertTrue(select([self.server.stdout], [], [], 5)[0], "Server did not start")
        self.assertIn("Listening on 127.0.0.1:", self.server.stdout.readline())

    def start_server(self, *options):
        return subprocess.Popen(
            [SERVER, str(self.port), *options],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True
        )

    def restart_with_policy(self, policy):
        self.stop_server()
        self.server = self.start_server(policy)
        self.wait_for_listener()

    def stop_server(self):
        if self.server.poll() is None:
            self.server.terminate()
            try:
                self.assertEqual(self.server.wait(timeout=3), 0)
            finally:
                if self.server.poll() is None:
                    self.server.kill()
                    self.server.wait()
        self.server.stdout.close()

    def connect(self):
        return socket.create_connection(("127.0.0.1", self.port), timeout=3)

    def rigctl(self, *commands):
        return subprocess.check_output(
            [CLIENT, "-m", "2", "-r", f"127.0.0.1:{self.port}", *commands],
            text=True, timeout=5,
        )

    def test_standard_client_and_simulated_ptt(self):
        self.rigctl("F", "7100000", "M", "USB", "2400")
        self.assertEqual(self.rigctl("f").strip(), "7100000")
        self.assertEqual(self.rigctl("m").splitlines(), ["USB", "2400"])
        self.rigctl("T", "1")
        self.assertEqual(self.rigctl("t").strip(), "1")
        self.rigctl("T", "0")
        self.assertEqual(self.rigctl("t").strip(), "0")

    def test_partial_command_leaves_second_client_responsive(self):
        with self.connect() as first, self.connect() as second, second.makefile("rb") as reply:
            first.sendall(b"F 710")
            second.sendall(b"+f\n")
            self.assertEqual(reply.readline(), b"get_freq:\n")
            self.assertTrue(reply.readline().startswith(b"Frequency: "))
            self.assertEqual(reply.readline(), b"RPRT 0\n")
            with first.makefile("rb") as first_reply:
                first.sendall(b"0000\r\nf\n")
                self.assertEqual(first_reply.readline(), b"RPRT 0\n")
                self.assertEqual(first_reply.readline(), b"7100000\n")
                first.sendall(b"q\n")
                self.assertEqual(first_reply.readline(), b"RPRT 0\n")
                self.assertEqual(first_reply.readline(), b"")

    def test_bind_conflict_then_restart_with_idle_clients(self):
        second = self.start_server()
        with second.stdout:
            try:
                self.assertEqual(second.wait(timeout=3), 1)
            finally:
                if second.poll() is None:
                    second.kill()
                    second.wait()
        with self.connect() as idle:
            idle.sendall(b"F ")
            self.stop_server()
            self.assertEqual(idle.recv(16), b"")
        self.server = self.start_server()
        self.wait_for_listener()
        self.assertTrue(self.rigctl("f").strip().isdigit())

    def test_clients_cannot_exit_or_pause_embedded_server(self):
        with self.connect() as connection, connection.makefile("rb") as reply:
            for command in (b"\\halt\n", b"\\pause 999\n"):
                connection.sendall(command)
                self.assertRegex(reply.readline(), rb"^RPRT -[0-9]+\n$")
            connection.sendall(b"f\n")
            self.assertTrue(reply.readline().strip().isdigit())

    def test_radio_policy_blocks_ptt_by_default_and_keeps_cat_available(self):
        self.restart_with_policy("receive-only")
        self.assertTrue(self.rigctl("f").strip().isdigit())
        with self.connect() as connection, connection.makefile("rb") as reply:
            connection.sendall(b"T 1\n")
            self.assertRegex(reply.readline(), rb"^RPRT -[0-9]+\n$")
            connection.sendall(b"t\n")
            self.assertEqual(reply.readline(), b"0\n")
            connection.sendall(b"F 7100000\nf\n")
            self.assertEqual(reply.readline(), b"RPRT 0\n")
            self.assertEqual(reply.readline(), b"7100000\n")

    def test_radio_policy_releases_on_owner_disconnect(self):
        self.restart_with_policy("ptt")
        with self.connect() as other, other.makefile("rb") as other_reply:
            with self.connect() as owner, owner.makefile("rb") as owner_reply:
                owner.sendall(b"T 1\n")
                self.assertEqual(owner_reply.readline(), b"RPRT 0\n")
                other.sendall(b"T 1\n")
                self.assertRegex(other_reply.readline(), rb"^RPRT -[0-9]+\n$")
                other.sendall(b"t\n")
                self.assertEqual(other_reply.readline(), b"1\n")
            for _ in range(20):
                other.sendall(b"t\n")
                if other_reply.readline() == b"0\n":
                    break
                time.sleep(0.05)
            else:
                self.fail("Simulated PTT stayed asserted after the owner disconnected")


if __name__ == "__main__":
    unittest.main()
