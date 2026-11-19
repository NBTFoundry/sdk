# SPDX-License-Identifier: MPL-2.0
"""The libretro host (libnbt-retro) with tests/fake_core.c, under Xvfb: the
game list from the files folder, the pad's bindings by family, the pause
menu from a pad and from the iDrive controller, save states, battery saves
and the settings feeding core variables."""

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
APP = os.path.join(BUILD_DIR, "nbt-retro-fake")

SOUTH, EAST, VIEW, MENU, DOWN = 1 << 0, 1 << 1, 1 << 6, 1 << 7, 1 << 12
PUSH, BACK = 1 << 0, 1 << 2
XBOX, DUALSHOCK4 = 1, 2


def xvfb():
    r, w = os.pipe()
    proc = subprocess.Popen(
        ["Xvfb", "-displayfd", str(w), "-screen", "0", "1280x480x24", "-nolisten", "tcp"],
        pass_fds=(w,), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    os.close(w)
    number = b""
    while not number.endswith(b"\n"):
        ready, _, _ = select.select([r], [], [], 10)
        chunk = os.read(r, 1) if ready else b""
        if not chunk:
            break
        number += chunk
    os.close(r)
    return proc, ":" + number.decode().strip()


class App:
    """The application with its stdout (the fake core's events) read by line."""

    def __init__(self, env, pass_fds):
        full = {k: v for k, v in os.environ.items() if not k.startswith("NBT_")}
        full.update(env)
        self.proc = subprocess.Popen([APP], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                     env=full, pass_fds=pass_fds)
        self.lines, self.buf = [], b""

    def wait(self, pattern, timeout=8.0):
        regex, deadline = re.compile(pattern), time.monotonic() + timeout
        while True:
            while b"\n" in self.buf:
                raw, self.buf = self.buf.split(b"\n", 1)
                self.lines.append(raw.decode())
            for i, line in enumerate(self.lines):
                if regex.search(line):
                    del self.lines[: i + 1]
                    return line
            if self.proc.stdout.closed:  # after stop(): only what it printed
                raise AssertionError(f"no line matching {pattern!r}; saw {self.lines!r}")
            fd = self.proc.stdout.fileno()
            left = deadline - time.monotonic()
            ready, _, _ = select.select([fd], [], [], max(0.0, left)) if left > 0 else ([], [], [])
            chunk = os.read(fd, 4096) if ready else b""
            if not chunk:
                raise AssertionError(f"no line matching {pattern!r}; saw {self.lines!r}")
            self.buf += chunk

    def stop(self):
        if self.proc.poll() is None:
            self.proc.send_signal(signal.SIGTERM)
        out, err = self.proc.communicate(timeout=10)
        self.buf += out
        self.stderr = err.decode()
        return self.proc.returncode


@unittest.skipUnless(os.access(APP, os.X_OK), f"{APP} not built")
@unittest.skipUnless(shutil.which("Xvfb"), "Xvfb not installed")
class RetroHostTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="nbt-retro-")
        self.files = os.path.join(self.tmp, "files")
        self.data = os.path.join(self.tmp, "data")
        os.mkdir(self.files, 0o700)
        os.mkdir(self.data, 0o700)
        for name, data in (("Alpha.fake", b"ROMDATA"), ("Beta.FK", b"B"), ("notes.txt", b"x")):
            with open(os.path.join(self.files, name), "wb") as f:
                f.write(data)
        self.x, self.display = xvfb()
        self.pad_app, self.pad = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
        self.knob_app, self.knob = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
        self.seq = {"pad": 0, "knob": 0}
        self.rotation = 0

    def tearDown(self):
        for s in (self.pad_app, self.pad, self.knob_app, self.knob):
            s.close()
        self.x.terminate()
        self.x.wait(timeout=5)
        shutil.rmtree(self.tmp, ignore_errors=True)

    def start(self, **settings):
        env = {
            "NBT_LAUNCH": "user", "DISPLAY": self.display, "LIBGL_ALWAYS_SOFTWARE": "1",
            "NBT_NO_AUDIO": "1", "NBT_FILES_DIR": self.files, "NBT_DATA_DIR": self.data,
            "NBT_BRIDGE_CAPS": "controller,gamepad",
            "NBT_GAMEPAD_FD": str(self.pad_app.fileno()),
            "NBT_CONTROLLER_FD": str(self.knob_app.fileno()),
        }
        env.update({"NBT_SETTING_" + k.upper(): v for k, v in settings.items()})
        app = App(env, (self.pad_app.fileno(), self.knob_app.fileno()))
        app.wait(r"^init$")
        return app

    def press(self, buttons=0, family=XBOX, hold=0.12):
        self.seq["pad"] += 1
        self.pad.send(struct.pack("<BBBBIIhhhhHH", 1, 1, 1, family, self.seq["pad"], buttons,
                                  0, 0, 0, 0, 0, 0))
        time.sleep(hold)

    def knob_state(self, keys=0, turn=0, hold=0.12):
        self.seq["knob"] += 1
        self.rotation += turn
        self.knob.send(struct.pack("<BBBBIIi", 0x43, 1, 1, 0, self.seq["knob"], keys, self.rotation))
        time.sleep(hold)

    def test_a_game_is_played_saved_and_resumed(self):
        app = self.start(region="pal")
        # The list: the core's extensions in any case, by name; south chooses.
        self.press(0)
        self.press(SOUTH)
        self.press(0)
        line = app.wait(r"^load ")
        self.assertTrue(line.endswith("/Alpha.fake size=7"), line)
        app.wait(r"^option core_region=PAL$")
        app.wait(r"^input mask=0000$")
        # An Xbox pad by position: east (B) is the SNES pad's A.
        self.press(EAST)
        app.wait(r"^input mask=0100$")
        self.press(0)
        app.wait(r"^input mask=0000$")
        # A DualShock 4 the same: Circle is A.
        self.press(0, DUALSHOCK4)
        self.press(EAST, DUALSHOCK4)
        app.wait(r"^input mask=0100$")
        self.press(0, DUALSHOCK4)
        # View + Menu: the pause menu; down, then save a state.
        self.press(VIEW | MENU)
        self.press(0)
        self.press(DOWN)
        self.press(0)
        self.press(SOUTH)
        self.press(0)
        app.wait(r"^serialize$")
        state = os.path.join(self.data, "states", "Alpha.fake.state1")
        self.assertTrue(os.path.isfile(state))
        # The iDrive controller: a BACK hold opens the menu, two detents to
        # Load state, a push loads it.
        self.knob_state(0)
        self.knob_state(BACK, hold=0.9)
        self.knob_state(0)
        self.knob_state(0, turn=2)
        self.knob_state(PUSH)
        self.knob_state(0)
        app.wait(r"^unserialize frame=\d+$")
        self.assertEqual(app.stop(), 0)
        app.wait(r"^unload$")
        app.wait(r"^deinit$")
        save = os.path.join(self.data, "saves", "Alpha.fake.srm")
        self.assertEqual(os.path.getsize(save), 8192)
        with open(os.path.join(self.data, "last-game"), "rb") as f:
            self.assertEqual(f.read(), b"Alpha.fake")
        self.assertIn("DualShock 4 connected", app.stderr)

    def test_a_long_back_hold_quits(self):
        app = self.start()
        self.press(0)
        self.press(SOUTH)
        self.press(0)
        app.wait(r"^load ")
        self.knob_state(0)
        self.knob_state(BACK, hold=1.8)
        app.wait(r"^unload$")
        self.assertEqual(app.stop(), 0)

    def test_frame_skip_draws_one_frame_in_two(self):
        app = self.start(frameskip="1")
        self.press(0)
        self.press(SOUTH)
        self.press(0)
        app.wait(r"^load ")
        app.wait(r"^drawn 60 of 120$")
        self.assertEqual(app.stop(), 0)

    def test_without_frame_skip_every_frame_is_drawn(self):
        app = self.start()
        self.press(0)
        self.press(SOUTH)
        self.press(0)
        app.wait(r"^load ")
        app.wait(r"^drawn 120 of 120$")
        self.assertEqual(app.stop(), 0)

    def test_an_empty_folder_says_how_to_add_games(self):
        for name in os.listdir(self.files):
            os.unlink(os.path.join(self.files, name))
        app = self.start()
        self.press(0)
        self.press(SOUTH)  # the one item: Quit
        self.assertEqual(app.stop(), 0)
        self.assertNotIn("load ", "\n".join(app.lines))


if __name__ == "__main__":
    unittest.main()
