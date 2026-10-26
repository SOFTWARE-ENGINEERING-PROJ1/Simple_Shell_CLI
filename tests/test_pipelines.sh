#!/bin/sh

SHELL_BIN="${SHELL_BIN:-./shell}"
PASS=0
FAIL=0
TMP_DIR=$(mktemp -d)

cleanup() {
    rm -rf "$TMP_DIR"
}
trap cleanup EXIT HUP INT TERM

check() {
    name="$1"
    expected="$2"
    actual="$3"

    if [ "$expected" = "$actual" ]; then
        PASS=$((PASS + 1))
        printf 'PASS: %s\n' "$name"
    else
        FAIL=$((FAIL + 1))
        printf 'FAIL: %s\n  Expected: <%s>\n  Actual:   <%s>\n' \
            "$name" "$expected" "$actual"
    fi
}

# 1. Output redirection >
actual=$(printf '%s\n' "/bin/echo 'redirection test' > $TMP_DIR/out1.txt" 'exit' | "$SHELL_BIN" 2>/dev/null)
file_content=$(cat "$TMP_DIR/out1.txt" 2>/dev/null)
check "output redirection (>)" "redirection test" "$file_content"

# 2. Append redirection >>
actual=$(printf '%s\n' "/bin/echo 'line1' > $TMP_DIR/out2.txt" "/bin/echo 'line2' >> $TMP_DIR/out2.txt" 'exit' | "$SHELL_BIN" 2>/dev/null)
file_content=$(cat "$TMP_DIR/out2.txt" 2>/dev/null)
check "append redirection (>>)" "$(printf 'line1\nline2')" "$file_content"

# 3. Input redirection <
printf 'input data from file\n' > "$TMP_DIR/in.txt"
actual=$(printf '%s\n' "/bin/cat < $TMP_DIR/in.txt" 'exit' | "$SHELL_BIN" 2>/dev/null)
check "input redirection (<)" "input data from file" "$actual"

# 4. Stderr redirection 2>
actual=$(printf '%s\n' "s4_nonexistent_command 2> $TMP_DIR/err.txt" 'exit' | "$SHELL_BIN" 2>/dev/null)
file_content=$(cat "$TMP_DIR/err.txt" 2>/dev/null)
case "$file_content" in
    *"s4_nonexistent_command"*) check "stderr redirection (2>)" "yes" "yes" ;;
    *) check "stderr redirection (2>)" "yes" "no" ;;
esac

# 5. Combined input and output redirection (< and >)
actual=$(printf '%s\n' "/bin/cat < $TMP_DIR/in.txt > $TMP_DIR/combined.txt" 'exit' | "$SHELL_BIN" 2>/dev/null)
file_content=$(cat "$TMP_DIR/combined.txt" 2>/dev/null)
check "combined input and output (< and >)" "input data from file" "$file_content"

# 6. Two-stage pipeline (cmd1 | cmd2)
actual=$(printf '%s\n' "/usr/bin/printf 'apple\nbanana\ncherry\n' | grep banana" 'exit' | "$SHELL_BIN" 2>/dev/null)
check "two-stage pipeline (cmd1 | cmd2)" "banana" "$actual"

# 7. Chained 3-stage pipeline (cmd1 | cmd2 | cmd3)
actual=$(printf '%s\n' "/usr/bin/printf 'zebra\napple\nmango\n' | /usr/bin/sort | /usr/bin/head -n 1" 'exit' | "$SHELL_BIN" 2>/dev/null)
check "chained pipeline (cmd1 | cmd2 | cmd3)" "apple" "$actual"

# 8. Pipeline with built-in command (echo | external)
actual=$(printf '%s\n' "echo pipeline_builtin_test | /usr/bin/tr a-z A-Z" 'exit' | "$SHELL_BIN" 2>/dev/null)
check "pipeline with built-in echo" "PIPELINE_BUILTIN_TEST" "$actual"

# 9. Pipeline exit status is from the LAST command
actual=$(printf '%s\n' 'false | true' '/usr/bin/printf "STATUS=%s\n" "$?"' 'exit' | "$SHELL_BIN" 2>/dev/null)
check "pipeline exit status matches last command (true)" "STATUS=0" "$actual"

actual=$(printf '%s\n' 'true | false' '/usr/bin/printf "STATUS=%s\n" "$?"' 'exit' | "$SHELL_BIN" 2>/dev/null)
check "pipeline exit status matches last command (false)" "STATUS=1" "$actual"

# 10. Chained pipeline completes without deadlock on large data
actual=$(printf '%s\n' '/usr/bin/yes | /usr/bin/head -n 5' 'exit' | "$SHELL_BIN" 2>/dev/null)
count=$(printf '%s\n' "$actual" | wc -l | tr -d ' ')
check "chained pipeline completes without deadlock" "5" "$count"

# 11. Pipeline syntax error (starting with pipe)
actual=$(printf '%s\n' '| /bin/echo error' 'exit' | "$SHELL_BIN" 2>&1)
case "$actual" in
    *"syntax error"*) check "syntax error on leading pipe" "yes" "yes" ;;
    *) check "syntax error on leading pipe" "yes" "no" ;;
esac

# 12. Pipeline syntax error (trailing pipe)
actual=$(printf '%s\n' '/bin/echo error |' 'exit' | "$SHELL_BIN" 2>&1)
case "$actual" in
    *"syntax error"*) check "syntax error on trailing pipe" "yes" "yes" ;;
    *) check "syntax error on trailing pipe" "yes" "no" ;;
esac

printf '\nS4 Redirection & Pipeline Tests: Passed %s  Failed %s\n' "$PASS" "$FAIL"

[ "$FAIL" -eq 0 ]
