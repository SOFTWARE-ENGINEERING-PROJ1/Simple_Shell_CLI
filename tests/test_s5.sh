#!/bin/sh
# S5 regression tests: signals sent to the shell, expansion-is-not-syntax,
# exit status on every error path, error recovery, zombies and fd leaks.
# Real Ctrl-C / Ctrl-Z keystrokes are tested in tests/test_tty.py.
# Usage: SHELL_BIN=./shell sh tests/test_s5.sh

SHELL_BIN=${SHELL_BIN:-./shell}
# The tests run inside a temp dir, so make the shell path absolute first.
case "$SHELL_BIN" in
    /*) ;;
    *) SHELL_BIN="$(cd "$(dirname "$SHELL_BIN")" && pwd)/$(basename "$SHELL_BIN")" ;;
esac
pass=0
fail=0
TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT HUP INT TERM
NL='
'

# check NAME INPUT EXPECTED_STDOUT
check() {
    actual=$(printf '%s\n' "$2" | "$SHELL_BIN" 2>/dev/null)
    result "$1" "$3" "$actual"
}

# result NAME EXPECTED ACTUAL
result() {
    if [ "$2" = "$3" ]; then
        pass=$((pass + 1))
        echo "PASS  $1"
    else
        fail=$((fail + 1))
        echo "FAIL  $1"
        echo "      expected: $2"
        echo "      actual:   $3"
    fi
}

# check_stderr NAME INPUT EXPECTED_STDERR_SUBSTRING
check_stderr() {
    err=$(printf '%s\n' "$2" | "$SHELL_BIN" 2>&1 >/dev/null)
    case "$err" in
        *"$3"*) result "$1" x x ;;
        *) result "$1" "stderr containing '$3'" "$err" ;;
    esac
}

cd "$TMP_DIR" || exit 1

echo "== Expansion is not syntax (S5 §3) =="
check "variable holding | is an argument, not a pipe" \
    "export P='|'${NL}echo before \$P after" "before | after"
check "variable holding > does not redirect" \
    "export R='>'${NL}echo hi \$R oops.txt" "hi > oops.txt"
[ -e oops.txt ] && result "no file created by \$R" "no oops.txt" "oops.txt exists" \
                || result "no file created by \$R" x x
check "variable holding < does not redirect" \
    "export L='<'${NL}echo in \$L nofile" "in < nofile"
check "\${VAR} holding | is an argument" \
    "export P='|'${NL}echo \${P}x" "|x"
check "single-quoted | is an argument" "echo '|'" "|"
check "double-quoted > is an argument" "echo \">\" x" "> x"
check "quoted 2> is an argument" "echo '2>' x" "2> x"
check "2> inside a word is not an operator" "echo a2>/dev/null b${NL}echo ok" "ok"
check "real unquoted | still pipes" "echo piped | cat" "piped"

echo "== Exit status and error paths (S5 §4) =="
check "successful command sets \$? to 0" "true${NL}echo \$?" "0"
check "failing command sets non-zero \$?" "false${NL}echo \$?" "1"
check "pipeline \$? is the last command (false | true)" "false | true${NL}echo \$?" "0"
check "pipeline \$? is the last command (true | false)" "true | false${NL}echo \$?" "1"
check "pipeline \$? with different non-zero statuses" \
    "sh -c 'exit 3' | sh -c 'exit 5'${NL}echo \$?" "5"
check_stderr "missing command says 'command not found'" "nosuchcmd_xyz" "nosuchcmd_xyz: command not found"
check "missing command sets \$? to 127" "nosuchcmd_xyz${NL}echo \$?" "127"
printf 'not executable\n' > noexec.sh
check "permission failure sets \$? to 126" "./noexec.sh${NL}echo \$?" "126"
check "failed exec child does not keep running the shell loop" \
    "nosuchcmd_xyz${NL}echo once" "once"
check "missing input file sets \$? to 1" "cat < /no/such/file${NL}echo \$?" "1"
check_stderr "missing input file is reported" "cat < /no/such/file" "/no/such/file"
check "unwritable output path sets \$? to 1" "echo hi > /no/such/dir/x${NL}echo \$?" "1"
check "built-in stdout restored after failed redirection" \
    "echo hi > /no/such/dir/x${NL}echo visible" "visible"
check "external command with bad redirection then valid command" \
    "/bin/echo hi > /no/such/dir/x${NL}echo next" "next"
check "syntax error sets \$? to 2" "echo a |${NL}echo \$?" "2"

echo "== Syntax-error recovery (S5 §5) =="
check "leading pipe, then valid command" "| echo x${NL}echo ok" "ok"
check "trailing pipe, then valid command" "echo x |${NL}echo ok" "ok"
check "double pipe, then valid command" "echo x || echo y${NL}echo ok" "ok"
check "missing redirection target, then valid command" "echo x >${NL}echo ok" "ok"
check_stderr "redirection target that is an operator" "cat < > x" "syntax error near unexpected token '>'"
check "operator as target, then valid command" "cat < > x${NL}echo ok" "ok"
[ -e x ] && result "no file created by 'cat < > x'" "no file x" "x exists" \
         || result "no file created by 'cat < > x'" x x
check "unclosed quote, then valid command" "echo \"abc${NL}echo ok" "ok"
check "whitespace-only line ignored" "   	  ${NL}echo ok" "ok"

echo "== Shell survives signals sent to it (FR-07.3) =="
for sig in INT TSTP; do
    out=$( { sleep 0.3; kill -s "$sig" "$(cat shell.pid)"; sleep 0.3; echo "echo alive-after-$sig"; } \
        | sh -c 'echo $$ > shell.pid; exec "$1"' sh "$SHELL_BIN" 2>/dev/null )
    result "shell keeps running after SIG$sig" "alive-after-$sig" "$out"
done

echo "== No zombies or descriptor leaks after errors (S5 §4) =="
cat > zombies.sh <<'EOF'
#!/bin/sh
# Count zombie children of the given PID.
ps -A -o ppid=,stat= | awk -v p="$1" '$1 == p && $2 ~ /^Z/' | wc -l | tr -d ' '
EOF
chmod +x zombies.sh
work="echo a | cat${NL}false | true | cat${NL}nosuchcmd_xyz | cat${NL}cat < /no/such${NL}echo x > /no/such/dir/y${NL}/bin/echo x > /no/such/dir/y${NL}| bad${NL}cat < > x"
out=$(printf '%s\n' "$work" "./zombies.sh \$\$" | "$SHELL_BIN" 2>/dev/null | tail -n 1)
result "no zombie children after pipelines and failures" "0" "$out"
fds=$(printf '%s\n' "/bin/ls /dev/fd" "$work" "/bin/ls /dev/fd" | "$SHELL_BIN" 2>/dev/null \
    | grep -v '^[ax]$' | tr '\n' ' ')
before=${fds%% 0 *}
after="0 ${fds#* 0 }"
result "no file descriptors leaked into later commands" "$before " "$after"

echo
echo "Passed: $pass  Failed: $fail"
[ "$fail" -eq 0 ]
