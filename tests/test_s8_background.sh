
#!/usr/bin/env bash

set -u

SHELL_BIN="${SHELL_BIN:-./shell}"
PASS=0
FAIL=0

check() {
    local name="$1"
    local condition="$2"

    if eval "$condition"; then
        echo "PASS: $name"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $name"
        FAIL=$((FAIL + 1))
    fi
}

# Test 1: Background execution returns immediately.
start_time=$(date +%s)
output=$(printf 'sleep 3 &\necho prompt_returned\nexit\n' | timeout 5 "$SHELL_BIN" 2>&1)
end_time=$(date +%s)
duration=$((end_time - start_time))

check "Background command does not block the shell" \
    '[ "$duration" -lt 3 ]'

check "Next command executes immediately" \
    '[[ "$output" == *"prompt_returned"* ]]'

check "Background job displays job ID and PID" \
    '[[ "$output" =~ \[[0-9]+\][[:space:]]+[0-9]+ ]]'

# Test 2: Foreground command runs while background job exists.
output=$(printf 'sleep 2 &\necho foreground_ok\nexit\n' | timeout 5 "$SHELL_BIN" 2>&1)

check "Foreground command runs alongside background job" \
    '[[ "$output" == *"foreground_ok"* ]]'

# Test 3: Background pipeline.
output=$(printf 'sleep 2 | cat &\necho pipeline_ok\nexit\n' | timeout 5 "$SHELL_BIN" 2>&1)

check "Background pipeline starts successfully" \
    '[[ "$output" == *"pipeline_ok"* ]]'

check "Background pipeline displays job information" \
    '[[ "$output" =~ \[[0-9]+\][[:space:]]+[0-9]+ ]]'

echo
echo "US-704 Background Execution Tests"
echo "Passed: $PASS"
echo "Failed: $FAIL"

if [ "$FAIL" -ne 0 ]; then
    exit 1
fi
