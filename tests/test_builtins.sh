#!/bin/sh
# Regression tests for S2: built-ins and environment expansion.
# Maps to TC-BUILTIN-01..08, TC-VAR-01..03 and TC-PARSE-02/03 in the Test Plan.
# Usage: ./tests/test_builtins.sh   (run from the repo root after `make`)

SHELL_BIN=${SHELL_BIN:-./shell}
pass=0
fail=0

# check NAME INPUT EXPECTED_STDOUT [FILTER]
# FILTER is an optional shell command the output is piped through first.
check() {
    actual=$(printf '%s\n' "$2" | "$SHELL_BIN" 2>/dev/null | sh -c "${4:-cat}")
    if [ "$actual" = "$3" ]; then
        pass=$((pass + 1))
        echo "PASS  $1"
    else
        fail=$((fail + 1))
        echo "FAIL  $1"
        echo "      input:    $2"
        echo "      expected: $3"
        echo "      actual:   $actual"
    fi
}

# check_status NAME INPUT EXPECTED_EXIT_CODE
check_status() {
    printf '%s\n' "$2" | "$SHELL_BIN" >/dev/null 2>&1
    code=$?
    if [ "$code" = "$3" ]; then
        pass=$((pass + 1))
        echo "PASS  $1"
    else
        fail=$((fail + 1))
        echo "FAIL  $1 (expected exit $3, got $code)"
    fi
}

# check_stderr NAME INPUT EXPECTED_STDERR_SUBSTRING
check_stderr() {
    err=$(printf '%s\n' "$2" | "$SHELL_BIN" 2>&1 >/dev/null)
    case "$err" in
        *"$3"*) pass=$((pass + 1)); echo "PASS  $1" ;;
        *) fail=$((fail + 1)); echo "FAIL  $1"; echo "      stderr: $err" ;;
    esac
}

NL='
'
TMP_REAL=$(cd /tmp && pwd -P)

# cd / pwd
check "TC-BUILTIN-01 cd /tmp; pwd" "cd /tmp${NL}pwd" "$TMP_REAL"
check "TC-BUILTIN-02 cd with no args goes to HOME" \
    "cd /tmp${NL}cd${NL}pwd" "$(cd "$HOME" && pwd -P)"
check_stderr "TC-BUILTIN-03 cd to missing dir reports error" \
    "cd /missing_dir_xyz" "cd: /missing_dir_xyz: No such file or directory"
check "TC-BUILTIN-03 shell continues after cd error" \
    "cd /missing_dir_xyz${NL}echo still alive" "still alive"
check "TC-BUILTIN-03 failed cd sets \$? to 1" "cd /missing_dir_xyz${NL}echo \$?" "1"
check "TC-BUILTIN-04 pwd prints absolute path" "cd /${NL}pwd" "/"
check "cd updates PWD and OLDPWD" "cd /${NL}cd /tmp${NL}echo \$OLDPWD \$PWD" "/ $TMP_REAL"

# echo
check "TC-BUILTIN-05 echo Hello World" "echo Hello World" "Hello World"
check "echo with no args prints empty line" "echo" ""
check "echo collapses unquoted whitespace" "echo a     b" "a b"

# export / unset
check "TC-BUILTIN-06 export sets variable" "export K=V${NL}echo \$K" "V"
check "TC-BUILTIN-06 unset removes variable" "export K=V${NL}unset K${NL}echo [\$K]" "[]"
check "export value containing =" "export K=a=b${NL}echo \$K" "a=b"
check "export with no args lists the environment" \
    "export ZZ_TEST=1${NL}export" 'export ZZ_TEST="1"' "grep ZZ_TEST"
check_stderr "export rejects invalid identifier" "export 1BAD=x" "not a valid identifier"
check "export error sets \$? to 1" "export 1BAD=x${NL}echo \$?" "1"
check_stderr "unset rejects invalid identifier" "unset 1BAD" "not a valid identifier"

# help
check "TC-BUILTIN-07 help lists all 7 built-ins" \
    "help" "$(printf '%s\n' 'cd' 'pwd' 'echo' 'export' 'unset' 'help' 'exit')" \
    "awk '/^  /{print \$1}'"

# exit
check_status "TC-BUILTIN-08 exit 42" "exit 42" 42
check_status "exit with no args exits 0" "exit" 0
check_status "exit with non-integer exits 1" "exit abc" 1
check_status "EOF exits 0" "" 0
check "exit stops processing further lines" "exit 0${NL}echo not reached" ""
check "exit with too many args does not exit" "exit 1 2${NL}echo alive" "alive"

# Variable expansion
check "TC-VAR-01 \$VAR" "export USER_T=alice${NL}echo \$USER_T" "alice"
check "TC-VAR-01 \${VAR}" "export USER_T=alice${NL}echo \${USER_T}" "alice"
check "\${VAR} adjacent to text" "export A=foo${NL}echo \${A}bar" "foobar"
check "unset variable expands to empty" "echo [\$NOPE_XYZ]" "[]"
check "unquoted empty expansion is dropped" "echo \$NOPE_XYZ x" "x"
check "TC-VAR-02 \$? after success" "cd /${NL}echo \$?" "0"
check "TC-VAR-02 \$? after unknown command" "xyz_invalid_prog_123${NL}echo \$?" "127"
# exec keeps the PID, so the wrapper's $$ is the shell's own PID.
pid_out=$(printf 'echo $$\n' | sh -c 'echo $$; exec "$1"' sh "$SHELL_BIN")
pid_expected=$(echo "$pid_out" | sed -n 1p)
pid_actual=$(echo "$pid_out" | sed -n 2p)
if [ -n "$pid_expected" ] && [ "$pid_expected" = "$pid_actual" ]; then
    pass=$((pass + 1)); echo "PASS  TC-VAR-03 \$\$ equals shell PID"
else
    fail=$((fail + 1)); echo "FAIL  TC-VAR-03 \$\$ ($pid_expected vs $pid_actual)"
fi
check "lone \$ is literal" "echo \$ 5\$" "\$ 5\$"
check_stderr "bad substitution reported" "echo \${1x}" "bad substitution"
check_stderr "missing } reported" "echo \${HOME" "missing '}'"
check "shell continues after expansion error" "echo \${1x}${NL}echo ok" "ok"

# Quoting interaction with expansion
check "TC-PARSE-02 single quotes are literal" "echo 'literal \$VAR'" "literal \$VAR"
check "TC-PARSE-03 double quotes expand and keep spaces" \
    "export A=X${NL}echo \"\$A   y\"" "X   y"
check "quoted empty string is kept as argument" "echo '' x" " x"

# Token limit: FR-02.4 requires at least 256 tokens per command (TC-PARSE-04)
many_args=""
many_expansions=""
expanded=""
n=1
while [ "$n" -le 300 ]; do
    many_args="$many_args arg$n"
    if [ "$n" -le 256 ]; then
        many_expansions="$many_expansions \"\${V}$n\""
        expanded="$expanded x$n"
    fi
    n=$((n + 1))
done
many_args=${many_args# }
many_expansions=${many_expansions# }
expanded=${expanded# }

check "TC-PARSE-04 300 arguments kept exactly and in order" \
    "echo $many_args" "$many_args"
check "TC-PARSE-04 300 arguments counted as separate tokens" \
    "echo $many_args" "300" "wc -w | tr -d ' '"
check "TC-PARSE-04 256 quoted expansions all expand" \
    "export V=x${NL}echo $many_expansions" "$expanded"
check "TC-PARSE-04 shell still works after a 300-token line" \
    "echo $many_args${NL}echo ok" "ok" "tail -n 1"

echo
echo "Passed: $pass  Failed: $fail"
[ "$fail" -eq 0 ]
