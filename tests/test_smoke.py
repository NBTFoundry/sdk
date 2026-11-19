"""Drives tests/smoke_app.cpp through the NBT Manager bridge contract: the
ready and lease descriptors, status, live settings, controller and gamepad
datagrams, signals, the crash handler and (with Xvfb) the host window."""

import fcntl
import os
import re
import select
import shutil
import signal
import socket
import struct
import subprocess
import tempfile
import time
import unittest

BUILD_DIR = os.environ.get(
    "NBT_SDK_BUILD_DIR", os.path.join(os.path.dirname(__file__), "..", "build")
)
SMOKE = os.environ.get("NBT_SDK_SMOKE", os.path.join(BUILD_DIR, "nbt-sdk-smoke"))

CONTROLLER_BACK = 1 << 2
GAMEPAD_SOUTH = 1 << 0


def controller_state(sequence, keys=0, rotation=0, connected=True):
    return struct.pack("<BBBBIIi", 0x43, 1, 1 if connected else 0, 0, sequence, keys, rotation)


def gamepad_state(sequence, buttons=0, connected=True):
    return struct.pack(
        "<BBBBIIhhhhHH", 1, 1, 1 if connected else 0, 0, sequence, buttons, 0, 0, 0, 0, 0, 0
    )


class Smoke:
    """One smoke_app process with its stdout read line by line."""

    def __init__(self, args=(), env=None, pass_fds=()):
        full_env = {
            key: value
            for key, value in os.environ.items()
            if not key.startswith("NBT_") or key.startswith("NBT_SDK_")
        }
        full_env.update(env or {})
        self.proc = subprocess.Popen(
            [SMOKE, *args],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=full_env,
            pass_fds=pass_fds,
        )
        self.lines = []
        self._buf = b""

    def _drain(self):
        while b"\n" in self._buf:
            raw, self._buf = self._buf.split(b"\n", 1)
            self.lines.append(raw.decode())

    def wait_line(self, pattern, timeout=5.0):
        regex = re.compile(pattern)
        deadline = time.monotonic() + timeout
        fd = self.proc.stdout.fileno()
        while True:
            self._drain()
            for line in self.lines:
                if regex.search(line):
                    self.lines.remove(line)
                    return line
            left = deadline - time.monotonic()
            ready, _, _ = select.select([fd], [], [], max(0.0, left)) if left > 0 else ([], [], [])
            chunk = os.read(fd, 4096) if ready else b""
            if not chunk:
                raise AssertionError(f"no line matching {pattern!r}; saw {self.lines!r}")
            self._buf += chunk

    def stop(self, sig=signal.SIGTERM, timeout=5.0):
        """Sends `sig` (None: only waits for the process to end), then collects
        the remaining output and returns the exit status."""
        if sig is not None and self.proc.poll() is None:
            self.proc.send_signal(sig)
        try:
            out, err = self.proc.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            out, err = self.proc.communicate()
        self._buf += out
        self._drain()
        self.stdout = "\n".join(self.lines) + self._buf.decode()
        self.stderr = err.decode()
        return self.proc.returncode


def read_byte(fd, timeout):
    ready, _, _ = select.select([fd], [], [], timeout)
    return os.read(fd, 1) if ready else b""


@unittest.skipUnless(os.access(SMOKE, os.X_OK), f"{SMOKE} not built")
class SmokeTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="nbt-sdk-smoke-")
        self.lease_path = os.path.join(self.tmp, "lease")
        with open(self.lease_path, "wb"):
            pass
        self.fds = set()

    def tearDown(self):
        for fd in self.fds:
            os.close(fd)
        shutil.rmtree(self.tmp, ignore_errors=True)

    def track(self, *fds):
        self.fds.update(fds)
        return fds if len(fds) > 1 else fds[0]

    def close(self, fd):
        self.fds.discard(fd)
        os.close(fd)

    def bridge(self):
        """Ready pipe and lease file descriptors, as the manager passes them."""
        ready_r, ready_w = self.track(*os.pipe())
        lease = self.track(os.open(self.lease_path, os.O_RDWR))
        args = ["--ready-fd", str(ready_w), "--lease-fd", str(lease)]
        return ready_r, ready_w, lease, args

    def test_check_mode_ready_heartbeats_and_stop(self):
        ready_r, ready_w, lease, args = self.bridge()
        app = Smoke(args, {"NBT_LAUNCH": "check"}, (ready_w, lease))
        self.close(ready_w)
        app.wait_line(r"^start mode=check ")
        self.assertEqual(read_byte(ready_r, 3), b"R")
        self.assertEqual(read_byte(ready_r, 3), b"R")  # heartbeat
        self.assertEqual(read_byte(ready_r, 3), b"R")
        self.assertEqual(app.stop(), 0)
        self.assertIn("stop", app.stdout)

    def test_lease_conflict_exits_1(self):
        ready_r, ready_w, lease, args = self.bridge()
        holder = self.track(os.open(self.lease_path, os.O_RDWR))
        pid = os.fork()
        if pid == 0:  # another process holds the write lock
            fcntl.lockf(holder, fcntl.LOCK_EX)
            time.sleep(3)
            os._exit(0)
        self.addCleanup(lambda: os.waitpid(pid, 0))
        time.sleep(0.3)
        app = Smoke(args, {"NBT_LAUNCH": "user"}, (ready_w, lease))
        self.close(ready_w)
        self.assertEqual(app.stop(None), 1)
        self.assertEqual(read_byte(ready_r, 0.2), b"")
        os.kill(pid, signal.SIGKILL)

    def test_bad_descriptors_exit_2(self):
        ready_r, ready_w, lease, _ = self.bridge()
        for args in (
            ["--ready-fd", str(ready_w)],
            ["--ready-fd", str(ready_w), "--lease-fd", str(ready_w)],
            ["--ready-fd", "x", "--lease-fd", str(lease)],
        ):
            app = Smoke(args, {}, (ready_w, lease))
            self.assertEqual(app.stop(None), 2, args)

    def test_status_settings_and_signals(self):
        ready_r, ready_w, lease, args = self.bridge()
        status_path = os.path.join(self.tmp, "status")
        status = self.track(os.open(status_path, os.O_RDWR | os.O_CREAT, 0o600))
        settings_path = os.path.join(self.tmp, "settings")
        with open(os.open(settings_path, os.O_WRONLY | os.O_CREAT, 0o600), "w") as f:
            f.write("NBTVALUES1\nsound=off\n")
        status_text = os.path.join(self.tmp, "status-text")
        with open(status_text, "w") as f:
            f.write("E1M1 H:100%\twith a long tail\n")
        env = {
            "NBT_LAUNCH": "user",
            "NBT_BRIDGE_CAPS": "show,status,live-settings",
            "NBT_STATUS_FD": str(status),
            "NBT_SETTINGS_FILE": settings_path,
            "NBT_SETTING_SOUND": "on",
            "NBT_SETTING_TURN_SENS": "high",
            "NBT_SURFACE": "7",
        }
        app = Smoke([*args, "--status-file", status_text], env, (ready_w, lease, status))
        self.close(ready_w)
        app.wait_line(r"^start mode=user surface=7 caps=show,status,live-settings$")
        line = app.wait_line(r"^settings gen=0 ")
        self.assertIn("sound=off", line)  # the file overlays the environment
        self.assertIn("turn_sens=high", line)
        self.assertEqual(read_byte(ready_r, 3), b"R")
        time.sleep(1.3)
        with open(status_path) as f:
            self.assertRegex(f.read(), r"^\d+ E1M1 H:100% with a long \n$")
        with open(status_text, "w") as f:
            f.write("Menu\n")
        time.sleep(1.2)
        with open(status_path) as f:
            self.assertRegex(f.read(), r"^\d+ Menu\n$")  # shorter line: truncated

        # A live change, written in place as the manager does.
        with open(settings_path, "r+") as f:
            f.truncate(0)
            f.write("NBTVALUES1\nsound=on\n")
        line = app.wait_line(r"^settings gen=1 ", timeout=3)
        self.assertIn("sound=on", line)
        # Unsafe files are refused; the values stay.
        os.chmod(settings_path, 0o644)
        with open(settings_path, "r+") as f:
            f.truncate(0)
            f.write("NBTVALUES1\nsound=off\n")
        time.sleep(1.5)
        self.assertFalse(any(l.startswith("settings gen=2") for l in app.lines))

        app.proc.send_signal(signal.SIGUSR1)
        app.wait_line(r"^show$")
        self.assertEqual(app.stop(signal.SIGINT), 0)
        self.assertIn("live settings not applied: unsafe settings file", app.stderr)
        self.assertEqual(app.stderr.count("unsafe settings file"), 1)  # once per reason

    def test_controller_gamepad_and_back_gestures(self):
        controller_app, controller_mgr = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
        gamepad_app, gamepad_mgr = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
        pipe_r, pipe_w = self.track(*os.pipe())
        for sock in (controller_app, controller_mgr, gamepad_app, gamepad_mgr):
            self.addCleanup(sock.close)
        env = {
            "NBT_LAUNCH": "user",
            "NBT_BRIDGE_CAPS": "controller,gamepad",
            "NBT_CONTROLLER_FD": str(controller_app.fileno()),
            "NBT_GAMEPAD_FD": str(pipe_r),  # not a datagram socket: ignored
        }
        app = Smoke(["--gesture", "150,300,400,700"], env, (controller_app.fileno(), pipe_r))
        app.wait_line(r"^start mode=user ")
        controller_mgr.send(controller_state(1))
        controller_mgr.send(controller_state(2, CONTROLLER_BACK, rotation=3))
        line = app.wait_line(r"^controller keys=00000004 ")
        self.assertIn("pressed=00000004", line)
        self.assertIn("rotation=3", line)
        controller_mgr.send(controller_state(3, 0, rotation=3))
        app.wait_line(r"^back tap$")
        controller_mgr.send(controller_state(4, CONTROLLER_BACK, rotation=3))
        controller_mgr.send(controller_state(5, 0, rotation=3))
        app.wait_line(r"^back double$")
        controller_mgr.send(controller_state(6, CONTROLLER_BACK, rotation=3))
        app.wait_line(r"^back hold$", timeout=2)
        app.wait_line(r"^back long$", timeout=2)
        controller_mgr.send(controller_state(7, 0, rotation=3, connected=False))
        app.wait_line(r"link=0$")
        self.assertEqual(app.stop(), 0)
        self.assertNotIn("gamepad", app.stdout)

        env["NBT_GAMEPAD_FD"] = str(gamepad_app.fileno())
        env["NBT_BRIDGE_CAPS"] = "gamepad"
        app = Smoke([], env, (gamepad_app.fileno(),))
        app.wait_line(r"^start mode=user ")
        gamepad_mgr.send(gamepad_state(1))
        gamepad_mgr.send(gamepad_state(2, GAMEPAD_SOUTH))
        app.wait_line(r"^gamepad seq=2 buttons=00000001 link=1$")
        self.assertEqual(app.stop(), 0)

    def test_crash_handler_names_the_stage(self):
        app = Smoke(["--crash"], {"NBT_LAUNCH": "user"})
        rc = app.stop(None)
        self.assertEqual(rc, -signal.SIGSEGV)
        self.assertRegex(app.stderr, r"\[smoke\] FATAL sig=11 code=-?\d+ addr=0x[0-9a-f]+ .*stage=crashing")

    def test_closed_ready_pipe_stops_the_application(self):
        ready_r, ready_w, lease, args = self.bridge()
        app = Smoke(args, {"NBT_LAUNCH": "user"}, (ready_w, lease))
        self.close(ready_w)
        app.wait_line(r"^start ")
        self.assertEqual(read_byte(ready_r, 3), b"R")
        self.close(ready_r)  # the manager went away: the next heartbeat fails
        app.wait_line(r"^stop$", timeout=4)
        self.assertEqual(app.stop(), 0)

    @unittest.skipUnless(shutil.which("Xvfb"), "Xvfb not installed")
    def test_host_video_window(self):
        r, w = os.pipe()
        xvfb = subprocess.Popen(
            ["Xvfb", "-displayfd", str(w), "-screen", "0", "800x480x24", "-nolisten", "tcp"],
            pass_fds=(w,),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        os.close(w)
        try:
            display = b""
            while not display.endswith(b"\n"):
                chunk = read_byte(r, 10)
                if not chunk:
                    break
                display += chunk
            os.close(r)
            self.assertTrue(display.strip(), "Xvfb gave no display number")
            env = {
                "NBT_LAUNCH": "user",
                "DISPLAY": ":" + display.decode().strip(),
                "LIBGL_ALWAYS_SOFTWARE": "1",
            }
            app = Smoke(["--video"], env)
            app.wait_line(r"^video 640x240$", timeout=10)
            self.assertEqual(app.stop(), 0)
        finally:
            xvfb.terminate()
            xvfb.wait(timeout=5)


if __name__ == "__main__":
    unittest.main()
