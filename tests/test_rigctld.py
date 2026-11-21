"""Smoke-test the embedded server against the pinned Hamlib rigctl client."""

import socket
import struct
import subprocess
import sys
import time
import unittest
from pathlib import Path
from select import select


SERVER, CLIENT = map(lambda name: Path(name).resolve(), sys.argv[1:3])
del sys.argv[1:3]

# Hamlib v1 streaming header. Header fields are network-endian, PCM is little-endian.
STREAM_HEADER = struct.Struct("!BBHIIQIBBHHH")


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

    def test_dummy_receive_stream_with_concurrent_cat(self):
        self.check_receive_stream_with_cat()

    def test_opt_in_dummy_tx_audio_round_trips_without_ptt(self):
        self.restart_with_policy("dummy-tx-loopback")
        subprocess.run(
            [SERVER.with_name("test-dummy-tx-loopback"), f"127.0.0.1:{self.port}"],
            check=True, timeout=10,
        )
        with self.connect() as cat, cat.makefile("rb") as reply:
            cat.sendall(b"T 1\nt\n")
            self.assertRegex(reply.readline(), rb"^RPRT -[0-9]+\n$")
            self.assertEqual(reply.readline(), b"0\n")

    def test_session_audio_provider_with_concurrent_cat(self):
        self.restart_with_policy("audio-fixture")
        self.check_receive_stream_with_cat(sample=1234)

    def check_receive_stream_with_cat(self, sample=None):
        with self.connect() as cat, cat.makefile("rb") as cat_reply:
            with self.connect() as owner, owner.makefile("rb") as reply, socket.socket(
                socket.AF_INET, socket.SOCK_DGRAM
            ) as audio:
                for args in (b"AUDIO_TX PCM_S16 48000", b"AUDIO_RX PCM_S16 48000 multicast=239.1.2.3:40000"):
                    owner.sendall(b"+\\stream_open " + args + b"\n")
                    self.assertTrue(reply.readline().startswith(b"stream_open:"))
                    self.assertRegex(reply.readline(), rb"^RPRT -[0-9]+\n$")

                owner.sendall(b"+\\stream_open AUDIO_RX PCM_S16 48000 channels=1\n")
                self.assertTrue(reply.readline().startswith(b"stream_open:"))
                fields = {}
                while True:
                    line = reply.readline()
                    if line.startswith(b"RPRT"):
                        self.assertEqual(line, b"RPRT 0\n")
                        break
                    key, value = line.strip().split(b": ", 1)
                    fields[key] = value
                stream_id = int(fields[b"stream_id"])
                token = int(fields[b"subscribe_token"])
                port = int(fields[b"udp_port"])
                with open("/proc/net/udp") as udp_sockets:
                    matching = [line.split()[1] for line in udp_sockets if f":{port:04X} " in line]
                self.assertIn(f"0100007F:{port:04X}", matching)
                self.assertNotIn(f"00000000:{port:04X}", matching)

                audio.settimeout(2)
                audio.connect(("127.0.0.1", port))
                audio.send(STREAM_HEADER.pack(1, 0, stream_id, token, 0, 0, 48000, 2, 1, 0, 4, 0))
                ack = STREAM_HEADER.unpack(audio.recv(2048))
                self.assertEqual(ack[:4], (1, 0, stream_id, token))
                self.assertEqual(ack[-2:], (8, 0))

                cat.sendall(b"f\n")
                self.assertTrue(cat_reply.readline().strip().isdigit())
                status_command = f"+\\stream_status {stream_id}\n".encode()
                cat.sendall(status_command)
                self.assertTrue(cat_reply.readline().startswith(b"stream_status:"))
                self.assertRegex(cat_reply.readline(), rb"^RPRT -[0-9]+\n$")

                deadline = time.monotonic() + 2
                while time.monotonic() < deadline:
                    data = audio.recv(2048)
                    header = STREAM_HEADER.unpack_from(data)
                    control, length = header[-2:]
                    if control not in (0, 0x20):
                        continue
                    self.assertEqual(header[:4], (1, 0, stream_id, token))
                    self.assertEqual(header[6:9], (48000, 2, 1))
                    self.assertEqual(len(data), STREAM_HEADER.size + length)
                    pcm = data[STREAM_HEADER.size + (20 if control else 0):]
                    if any(pcm):
                        if sample is not None:
                            self.assertEqual(set(struct.iter_unpack("<h", pcm)), {(sample,)})
                        break
                else:
                    self.fail("No nonzero PCM arrived from the dummy backend")

            # A client reconnects with a clean session and never inherits its predecessor's stream.
            with self.connect() as new_owner, new_owner.makefile("rb") as new_reply:
                new_owner.sendall(b"+\\stream_list\n")
                self.assertTrue(new_reply.readline().startswith(b"stream_list:"))
                listing = []
                for line in new_reply:
                    if line.startswith(b"RPRT"):
                        self.assertEqual(line, b"RPRT 0\n")
                        break
                    listing.append(line)
                self.assertNotIn(f"stream_id: {stream_id}\n".encode(), listing)

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
