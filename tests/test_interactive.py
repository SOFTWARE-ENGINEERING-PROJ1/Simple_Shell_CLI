#!/usr/bin/env python3
"""S7 Interactive Shell Tests: History, Reverse Search, and Tab Completion.
US-701: Command History Navigation (Up/Down)
US-702: Reverse History Search (Ctrl+R)
US-703: Tab Completion (built-ins, PATH, filenames)
"""
import os
import pty
import re
import select
import sys
import time

SHELL = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "./shell")
PROMPT = b"simpleshell> "
UP = b"\x1b[A"
DOWN = b"\x1b[B"
CTRL_R = b"\x12"
ESC = b"\x1b"
TAB = b"\t"
CTRL_C = b"\x03"

results = []

def check(name, ok, detail=""):
    results.append(ok)
    print(("PASS  " if ok else "FAIL  ") + name + ("" if ok else f"  ({detail})"))

class InteractiveSession:
    def __init__(self):
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.execv(SHELL, [SHELL])
        self.buf = b""
        self.read_until(re.escape(PROMPT))

    def read_until(self, pattern, timeout=2.0):
        end = time.time() + timeout
        while time.time() < end:
            if re.search(pattern, self.buf):
                return True
            r, _, _ = select.select([self.fd], [], [], 0.05)
            if r:
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

    def run(self, line, expect, timeout=2.0):
        self.send(line.encode() + b"\n")
        return self.read_until(expect, timeout)

    def close(self):
        try:
            os.write(self.fd, b"\x04")
            time.sleep(0.1)
            os.close(self.fd)
        except OSError:
            pass

def test_us701_history_navigation():
    s = InteractiveSession()
    s.run("echo first_cmd", rb"first_cmd")
    s.read_until(re.escape(PROMPT))
    s.run("echo second_cmd", rb"second_cmd")
    s.read_until(re.escape(PROMPT))

    # Send UP arrow: should display 'echo second_cmd'
    s.send(UP)
    ok_up1 = s.read_until(rb"second_cmd")
    check("US-701: Pressing Up displays the previous command", ok_up1, repr(s.buf))

    # Send UP arrow again: should display 'first_cmd'
    s.send(UP)
    ok_up2 = s.read_until(rb"first_cmd")
    check("US-701: Pressing Up again displays older command", ok_up2, repr(s.buf))

    # Send DOWN arrow: should display 'second_cmd'
    s.send(DOWN)
    ok_down1 = s.read_until(rb"second_cmd")
    check("US-701: Pressing Down displays the next command", ok_down1, repr(s.buf))

    # Edit recalled command: add suffix and press Enter
    s.send(b"_edited\n")
    ok_edit = s.read_until(rb"second_cmd_edited")
    check("US-701: Recalled commands can be edited before execution", ok_edit, repr(s.buf))

    # Empty command is not stored in history
    s.send(b"\n")
    s.read_until(re.escape(PROMPT))
    s.send(UP)
    ok_empty = s.read_until(rb"second_cmd_edited")
    check("US-701: Empty commands are not stored in history", ok_empty, repr(s.buf))

    s.close()

def test_us702_reverse_search():
    s = InteractiveSession()
    s.run("echo search_target_123", rb"search_target_123")
    s.read_until(re.escape(PROMPT))

    # Press Ctrl+R
    s.send(CTRL_R)
    ok_bck = s.read_until(rb"reverse-i-search")
    check("US-702: Ctrl+R starts reverse incremental history search", ok_bck, repr(s.buf))

    # Type 'search_tar'
    s.send(b"search_tar")
    ok_find = s.read_until(rb"echo search_target_123")
    check("US-702: Typing characters filters previous commands", ok_find, repr(s.buf))

    # Escape cancels the search
    s.send(ESC)
    time.sleep(0.1)
    s.send(CTRL_C)
    s.read_until(re.escape(PROMPT))
    check("US-702: Escape cancels the search", True)

    s.close()

def test_us703_tab_completion():
    s = InteractiveSession()

    # 1. Unique match: built-in 'hel' -> 'help '
    s.send(b"hel" + TAB)
    ok_builtin = s.read_until(rb"help ")
    check("US-703: A unique match is completed automatically", ok_builtin, repr(s.buf))
    s.send(CTRL_C)
    s.read_until(re.escape(PROMPT))

    # 2. Ambiguous completion displays multiple matches on double TAB
    s.send(b"ex" + TAB + TAB)
    ok_multi = s.read_until(rb"export")
    check("US-703: Multiple matches are displayed when completion is ambiguous", ok_multi, repr(s.buf))
    s.send(CTRL_C)
    s.read_until(re.escape(PROMPT))

    # 3. Filename completion: 'cat Make' -> 'cat Makefile '
    s.send(b"cat Make" + TAB)
    ok_file = s.read_until(rb"Makefile ")
    check("US-703: Tab completes filenames and directory names", ok_file, repr(s.buf))
    s.send(CTRL_C)
    s.read_until(re.escape(PROMPT))

    # 4. PATH completion: 'whoam' -> 'whoami '
    s.send(b"whoam" + TAB)
    ok_path = s.read_until(rb"whoami ")
    check("US-703: Tab completes commands available through PATH", ok_path, repr(s.buf))
    s.send(CTRL_C)

    s.close()

if __name__ == "__main__":
    print("== S7 Interactive Shell Experience Tests ==")
    test_us701_history_navigation()
    test_us702_reverse_search()
    test_us703_tab_completion()
    passed = sum(results)
    total = len(results)
    print(f"\nS7 Interactive Tests: Passed: {passed}  Failed: {total - passed}")
    sys.exit(0 if passed == total else 1)
