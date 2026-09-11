import os
import hashlib
from pathlib import Path
import re
import select
import shlex
import struct
import subprocess
import tempfile
import time
import unittest


def request(*arguments):
    encoded = [argument.encode() if isinstance(argument, str) else argument for argument in arguments]
    body = struct.pack(">I", len(encoded))
    body += b"".join(struct.pack(">I", len(argument)) + argument for argument in encoded)
    return struct.pack(">I", len(body)) + body


class SessionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="nsptool-session-test-")
        cls.binary = Path(cls.directory.name) / "nsptool-mock"
        source = Path(__file__).resolve().parent
        root = source.parents[2]
        command = [os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-pthread"]
        command += shlex.split(os.environ.get("NSPTOOL_TEST_CFLAGS", ""))
        command += ["-I", str(root / "vendor/libnspire-src/src/api"), str(source / "session_mock.c"), "-o", str(cls.binary)]
        subprocess.run(command, check=True)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def setUp(self):
        self.log = Path(self.directory.name) / f"{self._testMethodName}.log"
        self.environment = dict(os.environ, NSPTOOL_TEST_LOG=str(self.log),
                                NSPTOOL_TEST_LOCK=str(Path(self.directory.name) / "usb.lock"))
        self.children = []

    def tearDown(self):
        for child in self.children:
            if child.poll() is None:
                child.kill()
            child.wait(timeout=5)
            for stream in (child.stdin, child.stdout, child.stderr):
                stream.close()

    def start(self, *arguments, environment=None, cwd=None):
        child = subprocess.Popen([str(self.binary), *(arguments or ("session",))],
                                 stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                 env=environment or self.environment, cwd=cwd, bufsize=0)
        self.children.append(child)
        return child

    def read_exact(self, child, length):
        received = bytearray()
        deadline = time.monotonic() + 5
        while len(received) < length:
            ready, _, _ = select.select([child.stdout], [], [], max(0, deadline - time.monotonic()))
            self.assertTrue(ready, "session response timed out")
            chunk = os.read(child.stdout.fileno(), length - len(received))
            self.assertTrue(chunk, "session response ended early")
            received.extend(chunk)
        return bytes(received)

    def response(self, child):
        status, length = struct.unpack(">II", self.read_exact(child, 8))
        self.assertLessEqual(length, 8 * 1024 * 1024)
        return status, self.read_exact(child, length).decode()

    def call(self, child, *arguments):
        child.stdin.write(request(*arguments))
        return self.response(child)

    def calls(self):
        return self.log.read_text().splitlines() if self.log.exists() else []

    def finish(self, child, expected=0):
        child.stdin.close()
        self.assertEqual(child.wait(timeout=5), expected)
        self.assertEqual(child.stdout.read(), b"")
        self.assertEqual(child.stderr.read(), b"")

    def test_version_and_lazy_init(self):
        version = subprocess.run([str(self.binary), "session-version"], capture_output=True, env=self.environment)
        self.assertEqual((version.returncode, version.stdout, version.stderr), (0, b"1\n", b""))
        child = self.start()
        self.assertEqual(self.call(child, "keys")[0], 0)
        self.finish(child)
        self.assertEqual(self.calls(), [])

    def test_keysvc_status_only_queries(self):
        child = self.start()
        status, output = self.call(child, "keysvc-status")
        self.assertEqual(status, 0)
        self.assertIn("keysvc version 2\nheld capacity 128\ncompatible yes\n", output)
        self.assertEqual(self.call(child, "keysvc-status"),
                         (0, "keysvc version 2\nheld capacity 128\ncompatible yes\n"))
        self.finish(child)
        self.assertEqual(self.calls(), ["init 1", "query 1", "query 1", "free 1"])

    def test_os_key_mapping_and_explicit_backend(self):
        child = self.start()
        names = ("esc", "enter", "up", "left", "a", "shift+a", "ctrl+c", "ctrl+home",
                 "shift+tab", "ctrl+left", "apostrophe", "ctrl+apostrophe", "ctrl+del",
                 "home", "menu", "right", "down", "ctrl+w", "help")
        expected = (0x1B9600, 0x0D1000, 0x001700, 0x000700, 0x616600, 0x416600, 0x03B200,
                    0x00FE00, 0x7C9500, 0x00DA00, 0x27F500, 0x240500, 0x00E304,
                    0x00FD00, 0x003600, 0x002700, 0x003700, 0x17C600, 0x007B00)
        status, output = self.call(child, "key-os", *names)
        self.assertEqual(status, 0, output)
        self.assertIn("transport acknowledged", output)
        self.assertIn("visible effect unverified", output)
        self.finish(child)
        self.assertEqual(self.calls(), ["init 1", *(f"key-os {key:06x} 1" for key in expected), "free 1"])

    def test_os_mapping_matches_immutable_upstream_oracle(self):
        # TiLP keysnsp.h at revision 6dba390e7390c4b98ae96287b39a3971c331fbef.
        header = (Path(__file__).resolve().parents[1] / "keysnsp.h").read_bytes()
        self.assertEqual(hashlib.sha256(header).hexdigest(),
                         "db631e846191313e117ad2e4bca20455bdd9a6de7e4376b0c1787d58339c3eab")
        excluded = {"CTRL", "SHIFT", "CTRL_SHIFT", "CTRL_A", "SHIFT_GRAB"}
        expected = {}
        for name, code in re.findall(rb"^#define KEYNSP_(\w+)\s+(0x[0-9A-F]+)$", header, re.M):
            name = name.decode()
            if name in excluded or "HOLD" in name:
                continue
            label = name.lower().replace("_", "-")
            for modifier in ("ctrl", "shift"):
                if label.startswith(modifier + "-"):
                    label = modifier + "+" + label[len(modifier) + 1:]
            expected[label.replace("tilde", "apostrophe")] = int(code, 16)
        child = self.start()
        status, output = self.call(child, "key-os", "--list")
        self.assertEqual(status, 0)
        names = output.splitlines()
        expected["help"] = 0x007B00
        self.assertEqual(len(names), 188)
        self.assertEqual(set(names), set(expected))
        self.assertEqual(self.calls(), [])
        self.assertEqual(self.call(child, "key-os", *names)[0], 0)
        self.finish(child)
        self.assertEqual(self.calls(), ["init 1", *(f"key-os {expected[name]:06x} 1" for name in names), "free 1"])

    def test_os_type_mapping(self):
        child = self.start()
        text = "aAuUvVwW09 +-*./^=<>?:;'\"|$()\n\t"
        codes = (0x616600, 0x416600, 0x756100, 0x556100, 0x764100, 0x564100,
                 0x772100, 0x572100, 0x305000, 0x393100, 0x202000, 0x2B1100,
                 0x2D1200, 0x2A1300, 0x2E7000, 0x2F1400, 0x5E9300, 0x3D7500,
                 0x3CA600, 0x3E8600, 0x3F0300, 0x3A0100, 0x3B0200, 0x27F500,
                 0x22A100, 0x7CED00, 0x240500, 0x285500, 0x293500, 0x0D1000, 0x099500)
        status, output = self.call(child, "type-os", text)
        self.assertEqual(status, 0, output)
        self.finish(child)
        self.assertEqual(self.calls(), ["init 1", *(f"key-os {key:06x} 1" for key in codes), "free 1"])

    def test_os_invalid_requests_do_not_connect(self):
        child = self.start()
        invalid = (("key-os",), ("type-os",), ("type-os", ""), ("type-os", "a", "b"),
                   ("key-os", "esc", "not-a-key"), ("key-os", "a", "+b"),
                   ("key-os", "a", "-b"), ("key-os", "release-all"), ("key-os", "restart"),
                   ("key-os", "ctrl"), ("key-os", "shift"), ("key-os", "ctrl+shift+a"),
                   ("key-os", "a+b"), ("key-os", "ctrl+a"), ("key-os", "shift-hold-left"),
                   ("key-os", "wait:1"), ("type-os", "validπ"), ("type-os", "valid🙂"),
                   ("type-os", "valid!"), ("type-os", "valid\r"), ("type-os", "a" * 4097))
        for arguments in invalid:
            self.assertEqual(self.call(child, *arguments)[0], 2, arguments)
            self.assertEqual(self.calls(), [], arguments)
        self.finish(child)

    def test_os_late_invalid_one_shot_does_not_connect(self):
        command = subprocess.run([str(self.binary), "type-os", "helloπ"],
                                 capture_output=True, env=self.environment, timeout=5)
        self.assertEqual(command.returncode, 2)
        self.assertEqual(self.calls(), [])

    def test_os_send_failure_stops_and_does_not_fallback(self):
        child = self.start(environment=dict(self.environment, NSPTOOL_TEST_KEY_OS_FAIL="2"))
        status, output = self.call(child, "key-os", "a", "b", "c")
        self.assertEqual(status, 1, output)
        self.assertIn("unconfirmed", output)
        self.assertIn("before retrying", output)
        self.finish(child)
        self.assertEqual(self.calls(), ["init 1", "key-os 616600 1", "key-os 624600 1", "free 1"])

    def test_os_init_failure_does_not_send(self):
        child = self.start(environment=dict(self.environment, NSPTOOL_TEST_INIT_FAIL="1"))
        self.assertEqual(self.call(child, "key-os", "esc")[0], 1)
        self.finish(child)
        self.assertEqual(self.calls(), ["init 1"])

    def test_reuses_handle_and_captures_native_output(self):
        child = self.start()
        status, output = self.call(child, "info")
        self.assertEqual(status, 0)
        self.assertIn("native init stdout", output)
        self.assertIn("native init stderr", output)
        self.assertIn("generation 1", output)
        status, output = self.call(child, "info")
        self.assertEqual(status, 0)
        self.assertNotIn("native init", output)
        self.assertIn("generation 1", output)
        self.finish(child)
        self.assertEqual(self.calls(), ["init 1", "info 1", "info 1", "free 1"])

    def test_failure_is_not_replayed_and_next_request_reconnects(self):
        child = self.start()
        self.assertEqual(self.call(child, "info")[0], 0)
        status, output = self.call(child, "mkdir", "fail")
        self.assertEqual(status, 1)
        self.assertIn("native free stdout", output)
        self.assertIn("native free stderr", output)
        self.assertEqual(self.calls(), ["init 1", "info 1", "mkdir 1", "free 1"])
        contender = subprocess.run([str(self.binary), "info"], env=self.environment, capture_output=True, timeout=5)
        self.assertEqual(contender.returncode, 0)
        self.assertIn("generation 2", self.call(child, "info")[1])
        self.finish(child)
        self.assertEqual(self.calls().count("mkdir 1"), 1)

    def test_init_failure_does_not_retry(self):
        child = self.start(environment=dict(self.environment, NSPTOOL_TEST_INIT_FAIL="1"))
        self.assertEqual(self.call(child, "info")[0], 1)
        self.assertEqual(self.calls(), ["init 1"])
        self.finish(child)

    def test_rmdir_uses_directory_api_and_retains_connection(self):
        path = "/ndless/startup folder"
        child = self.start(environment=dict(self.environment, NSPTOOL_TEST_RMDIR_PATH=path))
        self.assertEqual(self.call(child, "info")[0], 0)
        status, output = self.call(child, "rmdir", path)
        self.assertEqual(status, 0, output)
        self.assertEqual(output, f"removed directory {path}\n")
        self.assertEqual(self.call(child, "info")[0], 0)
        self.finish(child)
        self.assertEqual(self.calls(), ["init 1", "info 1", "ls 1", "rmdir 1", "info 1", "free 1"])

    def test_rmdir_one_shot_uses_directory_api(self):
        path = "/ndless/startup"
        command = subprocess.run([str(self.binary), "rmdir", path], capture_output=True,
                                 env=dict(self.environment, NSPTOOL_TEST_RMDIR_PATH=path), timeout=5)
        self.assertEqual(command.returncode, 0, command.stderr)
        self.assertIn(f"removed directory {path}\n".encode(), command.stdout)
        self.assertEqual(self.calls(), ["init 1", "ls 1", "rmdir 1", "free 1"])

    def test_rmdir_failure_does_not_recurse_fallback_or_replay(self):
        path = "/ndless"
        child = self.start(environment=dict(self.environment, NSPTOOL_TEST_RMDIR_PATH=path,
                                             NSPTOOL_TEST_RMDIR_FAIL="1"))
        status, output = self.call(child, "rmdir", path)
        self.assertEqual(status, 1, output)
        self.assertIn("rmdir: mock transport failure", output)
        self.assertNotIn("removed directory", output)
        self.assertEqual(self.calls(), ["init 1", "ls 1", "rmdir 1", "free 1"])
        self.assertEqual(self.call(child, "info")[0], 0)
        self.finish(child)
        self.assertEqual(self.calls(), ["init 1", "ls 1", "rmdir 1", "free 1", "init 2", "info 2", "free 2"])

    def test_rmdir_refuses_nonempty_directory_without_deletion(self):
        path = "/ndless"
        child = self.start(environment=dict(self.environment, NSPTOOL_TEST_RMDIR_PATH=path,
                                             NSPTOOL_TEST_RMDIR_NONEMPTY="1"))
        status, output = self.call(child, "rmdir", path)
        self.assertEqual(status, 1, output)
        self.assertIn(f"rmdir: directory is not empty: {path}", output)
        self.assertNotIn("removed directory", output)
        self.finish(child)
        self.assertEqual(self.calls(), ["init 1", "ls 1", "free 1"])

    def test_rmdir_listing_failure_withholds_deletion(self):
        path = "/ndless"
        child = self.start(environment=dict(self.environment, NSPTOOL_TEST_RMDIR_PATH=path,
                                             NSPTOOL_TEST_DIRLIST_FAIL="1"))
        status, output = self.call(child, "rmdir", path)
        self.assertEqual(status, 1, output)
        self.assertIn("rmdir: mock transport failure", output)
        self.assertNotIn("removed directory", output)
        self.finish(child)
        self.assertEqual(self.calls(), ["init 1", "ls 1", "free 1"])

    def test_rmdir_requires_one_path(self):
        child = self.start()
        for arguments in (("rmdir",), ("rmdir", "/ndless", "/nps"), ("rmdir", "-r", "/ndless")):
            status, output = self.call(child, *arguments)
            self.assertEqual(status, 2, arguments)
            self.assertIn("rmdir <remote>", output)
        self.finish(child)
        self.assertFalse(any(call.startswith(("rmdir ", "rm ", "ls ")) for call in self.calls()))

    def test_actual_process_lock_contention_and_eof_release(self):
        child = self.start()
        self.assertEqual(self.call(child, "info")[0], 0)
        contender = subprocess.run([str(self.binary), "info"], env=self.environment, cwd="/", capture_output=True, timeout=5)
        self.assertEqual(contender.returncode, 1)
        self.assertIn(f"USB busy: nsptool process {child.pid}".encode(), contender.stderr)
        self.assertEqual(self.calls(), ["init 1", "info 1"])
        session = self.start(cwd="/")
        self.assertIn("USB busy", self.call(session, "info")[1])
        self.finish(child)
        self.assertEqual(self.call(session, "info")[0], 0)
        self.finish(session)

    def test_killed_process_releases_lock(self):
        child = self.start()
        self.assertEqual(self.call(child, "info")[0], 0)
        child.kill()
        child.wait(timeout=5)
        contender = subprocess.run([str(self.binary), "info"], env=self.environment, capture_output=True, timeout=5)
        self.assertEqual(contender.returncode, 0)

    def test_dispatch_file_screenshot_key_and_type(self):
        child = self.start()
        local = Path(self.directory.name) / "download"
        screen = Path(self.directory.name) / "screen.ppm"
        for arguments in (("get", "/remote", str(local)), ("put", str(local), "/remote"),
                          ("rm", "/remote"), ("ls", "/"), ("screenshot", str(screen)),
                          ("key", "a"), ("type", "abc"), ("restart",)):
            self.assertEqual(self.call(child, *arguments)[0], 0, arguments)
        self.finish(child)
        self.assertEqual(local.read_bytes(), b"abc")
        self.assertEqual(screen.read_bytes(), b"P6\n1 1\n255\n\x7f\x7f\x7f")
        self.assertEqual(self.calls().count("init 1"), 1)

    def test_fragmented_and_concatenated_frames(self):
        child = self.start()
        for value in request("info"):
            child.stdin.write(bytes([value]))
        self.assertEqual(self.response(child)[0], 0)
        child.stdin.write(request("info") + request("info"))
        self.assertEqual(self.response(child)[0], 0)
        self.assertEqual(self.response(child)[0], 0)
        self.finish(child)
        self.assertEqual(self.calls().count("info 1"), 3)

    def test_malformed_partial_and_oversized_frames(self):
        bodies = (b"", struct.pack(">I", 0), struct.pack(">I", 8193),
                  struct.pack(">II", 1, 9) + b"info", struct.pack(">II", 1, 5) + b"i\x00nfo",
                  struct.pack(">II", 1, 4) + b"info" + b"tail")
        frames = [struct.pack(">I", len(body)) + body for body in bodies]
        frames += [b"\x00", b"\x00\x00\x00", struct.pack(">I", 1048577), request("info")[:-1]]
        frames += [request("mkdir", argument) for argument in
                   (b"\xc0\x80", b"\xed\xa0\x80", b"\xf4\x90\x80\x80", b"\xe2\x82", b"\xff", b"\xc2z")]
        for frame in frames:
            with self.subTest(frame=frame):
                child = self.start()
                child.stdin.write(frame)
                child.stdin.close()
                status, output = self.response(child)
                self.assertEqual(status, 2)
                self.assertIn("invalid or incomplete", output)
                self.assertEqual(child.wait(timeout=5), 2)
                self.assertEqual(child.stdout.read(), b"")
        self.assertEqual(self.calls(), [])

    def test_valid_utf8_arguments(self):
        child = self.start()
        status, output = self.call(child, "mkdir", "caf\u00e9-\u6570\u5b66-\U0001f4df")
        self.assertEqual(status, 0)
        self.assertIn("caf\u00e9-\u6570\u5b66-\U0001f4df", output)
        self.finish(child)

    def test_malformed_request_releases_retained_connection(self):
        child = self.start()
        self.assertEqual(self.call(child, "info")[0], 0)
        child.stdin.write(struct.pack(">I", 1048577))
        self.assertEqual(self.response(child)[0], 2)
        self.assertEqual(child.wait(timeout=5), 2)
        self.assertEqual(self.calls(), ["init 1", "info 1", "free 1"])
        contender = subprocess.run([str(self.binary), "info"], env=self.environment, capture_output=True, timeout=5)
        self.assertEqual(contender.returncode, 0)

    def test_output_bound_drains_without_deadlock(self):
        child = self.start()
        status, output = self.call(child, "mkdir", "flood")
        self.assertEqual(status, 1)
        self.assertIn("exceeded 8 MiB", output)
        self.assertEqual(child.wait(timeout=5), 1)
        self.assertEqual(self.calls(), ["init 1", "mkdir 1", "free 1"])

    def test_broken_response_pipe_releases_lock(self):
        child = self.start()
        self.assertEqual(self.call(child, "info")[0], 0)
        child.stdout.close()
        child.stdin.write(request("info"))
        self.assertEqual(child.wait(timeout=5), 1)
        contender = subprocess.run([str(self.binary), "info"], env=self.environment, capture_output=True, timeout=5)
        self.assertEqual(contender.returncode, 0)


if __name__ == "__main__":
    unittest.main()
