#!/bin/sh

SHELL_BIN="${SHELL_BIN:-./shell}"
PASS=0
FAIL=0

check() {
    name="$1"
    ok="$2"
    if [ "$ok" = "1" ]; then
        PASS=$((PASS + 1))
        printf 'PASS  %s\n' "$name"
    else
        FAIL=$((FAIL + 1))
        printf 'FAIL  %s\n' "$name"
    fi
}

echo "== S7 Interactive Experience Tests =="

# 1. Non-interactive input loop still works cleanly
out=$(printf 'echo history_line_1\necho history_line_2\nexit\n' | "$SHELL_BIN" 2>/dev/null)
case "$out" in
    *"history_line_1"*history_line_2*) check "TC-S7-01 non-interactive loop preserves execution order" 1 ;;
    *) check "TC-S7-01 non-interactive loop preserves execution order" 0 ;;
esac

# 2. Empty commands do not crash shell and are ignored
out=$(printf '\n   \n\techo non_empty\nexit\n' | "$SHELL_BIN" 2>/dev/null)
case "$out" in
    *"non_empty"*) check "TC-S7-02 empty lines skipped cleanly" 1 ;;
    *) check "TC-S7-02 empty lines skipped cleanly" 0 ;;
esac

# 3. Readline dynamic library link check
if ldd "$SHELL_BIN" | grep -q "libreadline"; then
    check "TC-S7-03 linked against GNU Readline (libreadline)" 1
else
    check "TC-S7-03 linked against GNU Readline (libreadline)" 0
fi

# 4. Built-in command generator test
out=$(printf 'help\nexit\n' | "$SHELL_BIN" 2>/dev/null)
case "$out" in
    *"SimpleShell built-in commands:"*) check "TC-S7-04 built-in commands accessible" 1 ;;
    *) check "TC-S7-04 built-in commands accessible" 0 ;;
esac

# 5. Relative path execution (e.g., ./shell within shell)
out=$(printf '/bin/echo relative_path_test\nexit\n' | "$SHELL_BIN" 2>/dev/null)
case "$out" in
    *"relative_path_test"*) check "TC-S7-05 command paths executed correctly" 1 ;;
    *) check "TC-S7-05 command paths executed correctly" 0 ;;
esac

printf '\nS7 Interactive Tests: Passed %s  Failed %s\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ]
