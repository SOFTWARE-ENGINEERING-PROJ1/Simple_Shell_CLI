
#!/usr/bin/env bash

set -u

SHELL_BIN="${SHELL_BIN:-./shell}"
PASS=0
FAIL=0
TMP_DIR=$(mktemp -d)

cleanup() {
    rm -rf "$TMP_DIR"
}
trap cleanup EXIT

run_shell() {
    printf '%s\n' "$1" | "$SHELL_BIN" 2>&1
}

check_contains() {
    local name="$1"
    local output="$2"
    local expected="$3"

    if [[ "$output" == *"$expected"* ]]; then
        echo "PASS: $name"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $name"
        echo "  Expected: $expected"
        echo "  Output: $output"
        FAIL=$((FAIL + 1))
    fi
}

check_not_contains() {
    local name="$1"
    local output="$2"
    local unexpected="$3"

    if [[ "$output" != *"$unexpected"* ]]; then
        echo "PASS: $name"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $name"
        echo "  Unexpected: $unexpected"
        echo "  Output: $output"
        FAIL=$((FAIL + 1))
    fi
}

# Test 1: Semicolon executes both commands.
output=$(run_shell 'echo first ; echo second')
check_contains "; executes first command" "$output" "first"
check_contains "; executes second command" "$output" "second"

# Test 2: && executes only after success.
output=$(run_shell 'true && echo success')
check_contains "&& executes after success" "$output" "success"

output=$(run_shell 'false && echo should_not_print')
check_not_contains "&& skips command after failure" "$output" "should_not_print"

# Test 3: || executes only after failure.
output=$(run_shell 'false || echo failure_handled')
check_contains "|| executes after failure" "$output" "failure_handled"

output=$(run_shell 'true || echo should_not_print')
check_not_contains "|| skips command after success" "$output" "should_not_print"

# Test 4: Combined logical operators.
output=$(run_shell 'false && echo A || echo B')
check_contains "Combined && and || execute correct branch" "$output" "B"
check_not_contains "Skipped branch is not executed" "$output" "A"

# Test 5: Exit status expansion.
output=$(run_shell 'false || echo status=$?')
check_contains "$? reflects previous command status" "$output" "status=1"

output=$(run_shell 'true && echo status=$?')
check_contains "$? after successful command" "$output" "status=0"

# Test 6: Pipelines combined with chaining.
output=$(run_shell 'echo hello | cat && echo pipeline_success')
check_contains "Pipeline works with &&" "$output" "pipeline_success"

output=$(run_shell 'false | true && echo pipeline_status_ok')
check_contains "Pipeline status uses last command" "$output" "pipeline_status_ok"

# Test 7: Redirection combined with chaining.
output=$(printf 'echo redirected > %s && echo redirect_success\n' \
    "$TMP_DIR/output.txt" | "$SHELL_BIN" 2>&1)

check_contains "Redirection works with &&" "$output" "redirect_success"

if [[ -f "$TMP_DIR/output.txt" ]] &&
   [[ "$(cat "$TMP_DIR/output.txt")" == "redirected" ]]; then
    echo "PASS: Redirected output has correct content"
    PASS=$((PASS + 1))
else
    echo "FAIL: Redirected output has correct content"
    FAIL=$((FAIL + 1))
fi

# Test 8: Syntax errors.
output=$(run_shell '&& echo invalid')
check_not_contains "Leading && does not execute command" "$output" "invalid"

output=$(run_shell 'echo invalid ||')
check_not_contains "Trailing || is rejected" "$output" "invalid"

output=$(run_shell 'echo invalid ; ; echo invalid2')
check_not_contains "Repeated separators are rejected" "$output" "invalid2"


# Test 9: Invalid adjacent operator runs are rejected before execution.
for invalid in \
    'echo FIRST ||| echo SECOND' \
    'echo FIRST &&& echo SECOND' \
    'echo FIRST ||& echo SECOND' \
    'echo FIRST |&& echo SECOND'; do
    output=$(run_shell "$invalid")
    check_contains "Invalid operator run is rejected: $invalid" "$output" "syntax error"
    check_not_contains "Invalid operator run executes no command: $invalid" "$output" "FIRST"
    check_not_contains "Invalid operator run executes no command: $invalid" "$output" "SECOND"
done

# Test 10: Validate every list segment before executing the first segment.
output=$(run_shell 'echo FIRST && echo SECOND | | cat')
check_contains "Invalid later pipeline is rejected before execution" "$output" "syntax error"
check_not_contains "Earlier command is not executed when later segment is invalid" "$output" "FIRST"
check_not_contains "Invalid later pipeline does not execute later command" "$output" "SECOND"

output=$(run_shell 'echo FIRST && echo SECOND >')
check_contains "Missing redirection target is rejected during pre-validation" "$output" "syntax error"
check_not_contains "No command executes when a later segment has invalid redirection" "$output" "FIRST"

echo
echo "US-706 Command Chaining Tests"
echo "Passed: $PASS"
echo "Failed: $FAIL"

if [[ "$FAIL" -ne 0 ]]; then
    exit 1
fi
