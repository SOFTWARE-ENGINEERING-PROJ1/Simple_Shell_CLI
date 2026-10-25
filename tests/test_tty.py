#!/usr/bin/env python3
"""S5 terminal tests: real Ctrl-C / Ctrl-Z keystrokes (SRS FR-07.1-07.3).

Runs the shell inside a pseudo-terminal, so the kernel's terminal driver
turns the ^C / ^Z bytes into SIGINT / SIGTSTP for the foreground process
group exactly as when a user presses the keys. Piped input cannot test this.

Usage: python3 tests/test_tty.py [path-to-shell]   (default ./shell)
"""
import os
import pty
import re
import select
import signal
import sys
import time

SHELL = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "./shell")
PROMPT = b"simpleshell> "
CTRL_C, CTRL_Z = b"\x03", b"\x1a"
results = []


def launch_like_login_shell():
    """Runs in the pty child (the session leader). Start the shell the way
    a login shell such as bash/zsh does: in its own process group, made
    the terminal's foreground group. Otherwise the shell's group would be
    orphaned, and the kernel discards Ctrl-Z for orphaned groups."""
    child = os.fork()
    if child == 0:
        os.setpgid(0, 0)
        signal.signal(signal.SIGTTOU, signal.SIG_IGN)
        os.tcsetpgrp(0, os.getpgrp())
        signal.signal(signal.SIGTTOU, signal.SIG_DFL)
        os.execv(SHELL, [SHELL])
    os.setpgid(child, child)
    _, status = os.waitpid(child, 0)
    os._exit(os.waitstatus_to_exitcode(status) & 0xFF)


class Session:
    """One shell running on its own pseudo-terminal, as in a real terminal."""

    def __init__(self):
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            launch_like_login_shell()
        self.buf = b""
        self.read_until(re.escape(PROMPT))
        self.run("echo shellpid=$$", rb"shellpid=\d+")
        self.shell_pid = int(re.search(rb"shellpid=(\d+)", self.buf).group(1))
        self.read_until(re.escape(PROMPT))

    def read_until(self, pattern, timeout=3.0):
        """Read output until regex `pattern` matches; return True if it did."""
        end = time.time() + timeout
        while time.time() < end:
            if re.search(pattern, self.buf):
                return True
            ready, _, _ = select.select([self.fd], [], [], 0.05)
            if ready:
                try:
                    data = os.read(self.fd, 4096)
                except OSError:
                    break
                if not data:
                    break
                self.buf += data
        return bool(re.search(pattern, self.buf))

    def send(self, data):
        self.buf = b""
        os.write(self.fd, data)

    def run(self, line, expect, timeout=3.0):
        self.send(line.encode() + b"\n")
        return self.read_until(expect, timeout)

    def alive(self):
        done, _ = os.waitpid(self.pid, os.WNOHANG)
        return done == 0

    def close(self):
        """Exit with Ctrl-D; return the shell's exit status (or None)."""
        if self.alive():
            os.write(self.fd, b"\x04")
        end = time.time() + 3
        while time.time() < end:
            done, status = os.waitpid(self.pid, os.WNOHANG)
            if done:
                os.close(self.fd)
                return os.waitstatus_to_exitcode(status)
            self.read_until(b"$^", 0.05)
        os.kill(self.pid, signal.SIGKILL)
        os.waitpid(self.pid, 0)
        os.close(self.fd)
        return None


def check(name, ok, detail=""):
    results.append(ok)
    print(("PASS  " if ok else "FAIL  ") + name + ("" if ok else f"  ({detail})"))


def pid_exists(pid):
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False


def last_status(s):
    s.run("echo status=$?", rb"status=\d+")
    m = re.search(rb"status=(\d+)", s.buf)
    return int(m.group(1)) if m else None


def test_ctrl_c_idle():
    s = Session()
    s.send(b"partial input" + CTRL_C)
    redrawn = s.read_until(re.escape(PROMPT))
    check("Ctrl-C at idle prompt: shell alive, prompt redrawn", s.alive() and redrawn)
    ok = s.run("echo after-idle-ctrl-c", rb"after-idle-ctrl-c\r?\n")
    check("Ctrl-C at idle prompt: typed text discarded, next command runs", ok, s.buf)
    check("Ctrl-C at idle prompt: Ctrl-D still exits with status 0", s.close() == 0)


def test_ctrl_c_child():
    s = Session()
    s.send(b"sleep 10\n")
    time.sleep(0.5)
    start = time.time()
    s.send(CTRL_C)
    back = s.read_until(re.escape(PROMPT), timeout=3)
    check("Ctrl-C during 'sleep 10': prompt returns quickly",
          back and time.time() - start < 2, f"{time.time() - start:.1f}s")
    check("Ctrl-C during 'sleep 10': shell survives", s.alive())
    check("Ctrl-C during 'sleep 10': $? is 130 (128+SIGINT)", last_status(s) == 130)
    prompts = s.buf.count(PROMPT)
    check("Ctrl-C during 'sleep 10': single fresh prompt (no duplicates)", prompts == 1,
          f"{prompts} prompts in {s.buf!r}")
    s.close()


def test_ctrl_c_pipeline():
    s = Session()
    s.send(b"sleep 10 | sleep 10 | cat\n")
    time.sleep(0.5)
    start = time.time()
    s.send(CTRL_C)
    back = s.read_until(re.escape(PROMPT), timeout=3)
    check("Ctrl-C during 3-stage pipeline: whole pipeline stops, prompt returns",
          back and time.time() - start < 2, f"{time.time() - start:.1f}s")
    check("Ctrl-C during pipeline: shell survives and runs next command",
          s.run("echo after-pipeline", rb"after-pipeline\r?\n"))
    s.close()


def test_ctrl_c_builtin_in_pipeline():
    s = Session()
    s.send(b"echo hi | sleep 10\n")
    time.sleep(0.5)
    s.send(CTRL_C)
    check("Ctrl-C during pipeline with a built-in stage: prompt returns",
          s.read_until(re.escape(PROMPT), timeout=3))
    s.close()


def find_sleep_pid(s):
    """Find the PID of the 'sleep 30' child the shell started."""
    for line in os.popen("ps -A -o pid=,ppid=,command=").read().splitlines():
        parts = line.split(None, 2)
        if len(parts) == 3 and parts[1] == str(s.shell_pid) and parts[2].startswith("sleep 30"):
            return int(parts[0])
    return None


def test_ctrl_z_child():
    s = Session()
    s.send(b"sleep 30\n")
    time.sleep(0.5)
    s.send(CTRL_Z)
    back = s.read_until(re.escape(PROMPT), timeout=3)
    check("Ctrl-Z during 'sleep 30': shell continues and prompt returns", back and s.alive())
    check("Ctrl-Z: shell reports the child as stopped (no silent hang)",
          b"[stopped] sleep" in s.buf, s.buf)
    status = last_status(s)
    check("Ctrl-Z: $? is 128+SIGTSTP", status == 128 + signal.SIGTSTP, status)
    pid = find_sleep_pid(s)
    state = os.popen(f"ps -o stat= -p {pid}").read().strip() if pid else ""
    check("Ctrl-Z: child is suspended (state T), not killed", state.startswith("T"),
          f"pid={pid} state={state!r}")
    check("Ctrl-Z: shell keeps working", s.run("echo still-usable", rb"still-usable\r?\n"))
    code = s.close()
    time.sleep(0.2)
    check("Ctrl-Z: Ctrl-D exits with status 0", code == 0, code)
    check("Ctrl-Z: suspended child is terminated when the shell exits",
          pid is not None and not pid_exists(pid), pid)


def test_ctrl_z_idle():
    s = Session()
    s.send(CTRL_Z)
    redrawn = s.read_until(re.escape(PROMPT))
    check("Ctrl-Z at idle prompt: shell is not suspended", redrawn and s.alive())
    check("Ctrl-Z at idle prompt: next command runs",
          s.run("echo after-idle-ctrl-z", rb"after-idle-ctrl-z\r?\n"))
    s.close()


if __name__ == "__main__":
    for test in (test_ctrl_c_idle, test_ctrl_c_child, test_ctrl_c_pipeline,
                 test_ctrl_c_builtin_in_pipeline, test_ctrl_z_child, test_ctrl_z_idle):
        try:
            test()
        except Exception as exc:  # e.g. the shell died mid-test
            check(f"{test.__name__} ran to completion", False, repr(exc))
    print(f"\nTTY tests: Passed: {results.count(True)}  Failed: {results.count(False)}")
    sys.exit(0 if all(results) else 1)
