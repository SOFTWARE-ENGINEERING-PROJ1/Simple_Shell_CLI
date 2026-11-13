
#!/usr/bin/env python3

import os
import pty
import re
import select
import signal
import sys
import time

SHELL = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "./shell")
PROMPT = b"simpleshell> "
CTRL_Z = b"\x1a"
CTRL_C = b"\x03"

passed = 0
failed = 0


class Session:
    def __init__(self):
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.execv(SHELL, [SHELL])
        self.buf = b""
        self.read_until(re.escape(PROMPT))

    def read_until(self, pattern, timeout=4):
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

    def run(self, command, expect, timeout=4):
        self.send(command.encode() + b"\n")
        return self.read_until(expect, timeout)

    def close(self):
        try:
            os.write(self.fd, b"\x04")
        except OSError:
            pass
        end = time.time() + 3
        while time.time() < end:
            done, status = os.waitpid(self.pid, os.WNOHANG)
            if done:
                os.close(self.fd)
                return os.waitstatus_to_exitcode(status)
            time.sleep(0.05)
        os.kill(self.pid, signal.SIGKILL)
        os.waitpid(self.pid, 0)
        os.close(self.fd)
        return None


def check(name, ok, detail=""):
    global passed, failed
    if ok:
        passed += 1
        print("PASS:", name)
    else:
        failed += 1
        print("FAIL:", name, detail)


def test_jobs_lists_running_job():
    s = Session()
    try:
        s.send(b"sleep 20 &\n")
        started = s.read_until(re.escape(PROMPT))
        check("Background job starts", started, s.buf)

        ok = s.run("jobs", rb"Running.*sleep 20")
        check("jobs lists running background job", ok, s.buf)
    finally:
        s.close()


def test_ctrl_z_and_bg():
    s = Session()
    try:
        s.send(b"sleep 20\n")
        time.sleep(0.4)
        s.send(CTRL_Z)

        stopped = s.read_until(rb"Stopped.*sleep 20")
        check("Ctrl+Z stops foreground job", stopped, s.buf)

        ok = s.run("jobs", rb"Stopped.*sleep 20")
        check("jobs lists stopped job", ok, s.buf)

        ok = s.run("bg %1", re.escape(PROMPT))
        check("bg resumes stopped job", ok, s.buf)

        ok = s.run("jobs", rb"Running.*sleep 20")
        check("Resumed job appears as running", ok, s.buf)
    finally:
        s.close()



def test_fg():
    s = Session()
    try:
        s.send(b"sleep 20 &\n")
        started = s.read_until(re.escape(PROMPT))
        check("Background job starts for fg test", started, s.buf)

        s.send(b"fg %1\n")
        time.sleep(0.5)

        # If fg is working, the shell should not return to its prompt.
        prompt_returned = re.search(re.escape(PROMPT), s.buf) is not None
        check("fg keeps the job in the foreground",
              not prompt_returned, s.buf)

        s.send(CTRL_C)
        returned = s.read_until(re.escape(PROMPT))
        check("Ctrl+C terminates foreground job and returns prompt",
              returned, s.buf)

        ok = s.run("jobs", re.escape(PROMPT))
        check("Terminated foreground job is not listed",
              ok and b"sleep 20" not in s.buf, s.buf)

    finally:
        s.close()



if __name__ == "__main__":
    test_jobs_lists_running_job()
    test_ctrl_z_and_bg()
    test_fg()

    print()
    print("US-705 Job Management Tests")
    print("Passed:", passed)
    print("Failed:", failed)

    sys.exit(1 if failed else 0)
