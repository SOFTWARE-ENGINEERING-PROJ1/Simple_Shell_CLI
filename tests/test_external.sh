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

# Create a temporary directory containing test executables.
mkdir -p "$TMP_DIR/bin"

cat > "$TMP_DIR/bin/s3_hello" <<'EOF'
#!/bin/sh
printf 'PATH_OK\n'
EOF
chmod +x "$TMP_DIR/bin/s3_hello"

cat > "$TMP_DIR/exit7" <<'EOF'
#!/bin/sh
exit 7
EOF
chmod +x "$TMP_DIR/exit7"

cat > "$TMP_DIR/no_permission" <<'EOF'
#!/bin/sh
printf 'SHOULD_NOT_RUN\n'
EOF
chmod -x "$TMP_DIR/no_permission"

printf 'quoted file works\n' > "$TMP_DIR/file with spaces.txt"

# 1. External command with arguments.
actual=$(printf '%s\n' '/usr/bin/printf "hello world\n"' 'exit' | "$SHELL_BIN" 2>/dev/null)
check "external command arguments" "hello world" "$actual"

# 2. Executable path containing a slash.
actual=$(printf '/bin/echo SLASH_PATH_OK\nexit\n' | "$SHELL_BIN" 2>/dev/null)
check "executable path containing slash" "SLASH_PATH_OK" "$actual"

# 3. PATH lookup.
actual=$(printf 's3_hello\nexit\n' | PATH="$TMP_DIR/bin:$PATH" "$SHELL_BIN" 2>/dev/null)
check "find command through PATH" "PATH_OK" "$actual"

# 4. Successful command with no output.
actual=$(printf '%s\n' 'true' '/usr/bin/printf "STATUS=%s\n" "$?"' 'exit' | "$SHELL_BIN" 2>/dev/null)
check "successful command status is zero" "STATUS=0" "$actual"

# 5. Multiple external commands run sequentially.
actual=$(printf '/bin/echo FIRST\n/bin/echo SECOND\nexit\n' | "$SHELL_BIN" 2>/dev/null)
check "multiple external commands" "$(printf 'FIRST\nSECOND')" "$actual"

# 6. Missing command produces a diagnostic.
actual=$(printf 's3_command_that_does_not_exist\nexit\n' | "$SHELL_BIN" 2>&1)
case "$actual" in
    *"s3_command_that_does_not_exist"*) check "missing command diagnostic" "yes" "yes" ;;
    *) check "missing command diagnostic" "yes" "no" ;;
esac

# 7. Shell continues after a missing command.
actual=$(printf 's3_command_that_does_not_exist\n/bin/echo SHELL_CONTINUES\nexit\n' | "$SHELL_BIN" 2>/dev/null)
check "shell continues after missing command" "SHELL_CONTINUES" "$actual"

# 8. Nonzero exit status is propagated to $?.
actual=$(printf '%s\n' "$TMP_DIR/exit7" '/usr/bin/printf "STATUS=%s\n" "$?"' 'exit' | "$SHELL_BIN" 2>/dev/null)
check "nonzero exit status propagated" "STATUS=7" "$actual"

# 9. Missing command sets status 127.
actual=$(printf '%s\n' 's3_command_that_does_not_exist' '/usr/bin/printf "STATUS=%s\n" "$?"' 'exit' | "$SHELL_BIN" 2>/dev/null)
check "missing command status is 127" "STATUS=127" "$actual"

# 10. Non-executable file fails gracefully.
actual=$(printf '%s\n' "$TMP_DIR/no_permission" 'exit' | "$SHELL_BIN" 2>&1)
case "$actual" in
    *"Permission denied"*) check "permission error diagnostic" "yes" "yes" ;;
    *) check "permission error diagnostic" "yes" "no" ;;
esac

# 11. Permission error sets status 126.
actual=$(printf '%s\n' "$TMP_DIR/no_permission" '/usr/bin/printf "STATUS=%s\n" "$?"' 'exit' | "$SHELL_BIN" 2>/dev/null)
check "permission error status is 126" "STATUS=126" "$actual"

# 12. Quoted filename with spaces stays one argument.
actual=$(printf '/bin/cat "%s"\nexit\n' "$TMP_DIR/file with spaces.txt" | "$SHELL_BIN" 2>/dev/null)
check "quoted filename with spaces" "quoted file works" "$actual"

# 13. Exported environment variable is visible to external programs.
actual=$(printf 'export S3_EXTERNAL_VALUE=export_works\n/usr/bin/printenv S3_EXTERNAL_VALUE\nexit\n' | "$SHELL_BIN" 2>/dev/null)
check "export visible to external command" "export_works" "$actual"

# 14. Variable expansion works in external command arguments.
actual=$(printf 'export S3_EXTERNAL_VALUE=variable_works\n/bin/echo "$S3_EXTERNAL_VALUE"\nexit\n' | "$SHELL_BIN" 2>/dev/null)
check "variable expansion in external arguments" "variable_works" "$actual"

# 15. Shell remains usable after an execution failure.
actual=$(printf 's3_command_that_does_not_exist\n/bin/echo CHILD_FAILURE_DID_NOT_STOP_PARENT\nexit\n' | "$SHELL_BIN" 2>/dev/null)
check "parent continues after child execution failure" "CHILD_FAILURE_DID_NOT_STOP_PARENT" "$actual"



# 16. Exit status updates after a failing command and then a successful one.
actual=$(printf '%s\n' \
    "$TMP_DIR/exit7" \
    '/usr/bin/printf "STATUS=%s\n" "$?"' \
    'true' \
    '/usr/bin/printf "STATUS=%s\n" "$?"' \
    'exit' | "$SHELL_BIN" 2>/dev/null)
check "status updates after failure then success" \
    "$(printf 'STATUS=7\nSTATUS=0')" "$actual"

# 17. Signal termination produces status 128 + signal number.
cat > "$TMP_DIR/signal_term" <<'EOF'
#!/bin/sh
kill -TERM $$
EOF
chmod +x "$TMP_DIR/signal_term"

actual=$(printf '%s\n' \
    "$TMP_DIR/signal_term" \
    '/usr/bin/printf "STATUS=%s\n" "$?"' \
    'exit' | "$SHELL_BIN" 2>/dev/null)
check "signal termination status is 143" "STATUS=143" "$actual"

printf '\nS3 External Command Tests: Passed %s  Failed %s\n' "$PASS" "$FAIL"

[ "$FAIL" -eq 0 ]