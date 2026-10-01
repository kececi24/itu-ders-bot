"""Offline terminal acceptance against the built setup executable."""
import errno
import json
import os
import pty
import select
import signal
import subprocess
import sys
import tempfile
import termios
import time
from pathlib import Path

EXE = str(Path(sys.argv[1]).resolve())


class Session:
    def __init__(self, directory):
        self.master, self.slave = pty.openpty()
        self.saved = termios.tcgetattr(self.slave)
        self.output = b""
        self.process = subprocess.Popen(
            [EXE, "--env-path", str(Path(directory) / ".env"),
             "--config-path", str(Path(directory) / "config.json")],
            stdin=self.slave, stdout=self.slave, stderr=self.slave,
            start_new_session=True,
        )

    def until(self, text):
        deadline = time.monotonic() + 5
        target = text.encode()
        while target not in self.output:
            assert time.monotonic() < deadline, (text, self.output)
            if select.select([self.master], [], [], 0.1)[0]:
                try:
                    chunk = os.read(self.master, 65536)
                except OSError as error:
                    if error.errno == errno.EIO:
                        break
                    raise
                if not chunk:
                    break
                self.output += chunk
        assert target in self.output, (text, self.output)

    def send(self, data):
        os.write(self.master, data)

    def finish(self, expected):
        try:
            assert self.process.wait(timeout=5) == expected, self.output
            while select.select([self.master], [], [], 0)[0]:
                try:
                    chunk = os.read(self.master, 65536)
                except OSError as error:
                    if error.errno == errno.EIO:
                        break
                    raise
                if not chunk:
                    break
                self.output += chunk
            restored = termios.tcgetattr(self.slave)
            # Darwin may set PENDIN when canonical mode resumes; it is kernel
            # pending-input bookkeeping, not a user terminal setting.
            restored[3] &= ~getattr(termios, "PENDIN", 0)
            expected_settings = list(self.saved)
            expected_settings[3] &= ~getattr(termios, "PENDIN", 0)
            assert restored == expected_settings, "terminal was not restored"
        finally:
            if self.process.poll() is None:
                self.process.kill()
                self.process.wait()
            os.close(self.master)
            os.close(self.slave)


def password_prompt(session):
    session.until("3. Exit")
    session.send(b"\r")
    session.until("Enter your username:")
    session.send(b"test-user\n")
    session.until("Enter your password:")
    # The prompt precedes the termios call; wait until echo is disabled.
    deadline = time.monotonic() + 2
    while termios.tcgetattr(session.slave)[3] & termios.ECHO:
        assert time.monotonic() < deadline
        time.sleep(0.005)
    assert termios.tcgetattr(session.slave)[3] & termios.ICANON


with tempfile.TemporaryDirectory(prefix="itu-pty-") as directory:
    result = subprocess.run([EXE], input=b"\n", capture_output=True)
    assert result.returncode != 0 and b"requires a terminal" in result.stderr

    # Up wraps from the first option to Exit. A split escape sequence works.
    session = Session(directory)
    session.until("3. Exit")
    session.send(b"\x1b")
    time.sleep(0.02)
    session.send(b"[A\r")
    session.finish(0)
    assert not (Path(directory) / ".env").exists()

    session = Session(directory)
    password_prompt(session)
    secret = b"Secret-DO-NOT-ECHO-319"
    # Canonical erase should remove X while keeping the password hidden.
    session.send(secret + b"X\x7f\n")
    session.finish(0)
    assert secret not in session.output
    assert 'ITU_PASSWORD="' + secret.decode() + '"' in (Path(directory) / ".env").read_text()
    assert (Path(directory) / ".env").stat().st_mode & 0o777 == 0o600

    for sig in (signal.SIGINT, signal.SIGTERM):
        for at_password in (False, True):
            session = Session(directory)
            if at_password:
                password_prompt(session)
            else:
                session.until("3. Exit")
            session.process.send_signal(sig)
            session.finish(1)

    session = Session(directory)
    password_prompt(session)
    session.send(b"\x04")
    session.finish(1)

    session = Session(directory)
    session.until("3. Exit")
    # Down cycles through all three choices and ends on config.
    session.send(b"\x1b[B" * 4 + b"\r")
    for prompt, answer in (
        ("Enter date", b"2028/02/29\n"),
        ("Enter time", b"09:01:02:003\n"),
        ("Enter lead", b"0\n"),
        ("Enter add", b"123, 456,123\n"),
        ("Enter drop", b"789\n"),
    ):
        session.until(prompt)
        session.send(answer)
    session.finish(0)
    config = json.loads((Path(directory) / "config.json").read_text())
    assert config["courses"] == {"crn": ["123", "456", "123"], "scrn": ["789"]}
    assert config["time"]["millisecond"] == 3
    assert config["time"]["lead_millisecond"] == 0

print("setup PTY acceptance passed")
