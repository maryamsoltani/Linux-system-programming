#!/usr/bin/env bash
# Automated test suite for the mini-shell (Week 07)
# Run from the repo root or via: make -C bncf test

set -euo pipefail

SHELL_BIN="$(dirname "$0")/../bncf/shell"
TEST_BIN="$(dirname "$0")/../bncf/Test"
TMPDIR_TEST="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_TEST"' EXIT

PASS=0
FAIL=0

check() {
    local desc="$1"
    local input="$2"
    local expected="$3"
    local actual
    actual=$(printf '%s\n' "$input" | "$SHELL_BIN" 2>/dev/null)
    if [ "$actual" = "$expected" ]; then
        echo "  PASS: $desc"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $desc"
        echo "        input:    $input"
        echo "        expected: $expected"
        echo "        got:      $actual"
        FAIL=$((FAIL + 1))
    fi
}

check_parse() {
    local desc="$1"
    local input="$2"
    if printf '%s\n' "$input" | "$TEST_BIN" -s /dev/stdin > /dev/null 2>&1; then
        echo "  PASS (parse): $desc"
        PASS=$((PASS + 1))
    else
        # Test reads from stdin by default; pipe to it
        if printf '%s\n' "$input" | "$TEST_BIN" > /dev/null 2>&1; then
            echo "  PASS (parse): $desc"
            PASS=$((PASS + 1))
        else
            echo "  FAIL (parse): $desc -- input: $input"
            FAIL=$((FAIL + 1))
        fi
    fi
}

echo "=== Mini-Shell Test Suite ==="
echo ""

echo "--- Simple commands ---"
check "echo with args"         "echo hello world"       "hello world"
check "multiple jobs (;)"      "echo a ; echo b"        "$(printf 'a\nb')"
check "empty line skipped"     ""                        ""

echo ""
echo "--- Pipelines ---"
check "pipe echo|cat"          "echo hello | cat"       "hello"
check "pipe echo|grep match"   "echo hello | grep hello" "hello"
check "pipe echo|grep no match" "echo hello | grep xyz"  ""
check "two-stage pipeline"     "echo hello world | wc -w" "2"

echo ""
echo "--- I/O Redirection ---"
OUT="$TMPDIR_TEST/out.txt"
printf '%s\n' "echo redirected > $OUT" | "$SHELL_BIN" > /dev/null 2>&1
check "output redirection file content" "cat $OUT" "redirected"

IN="$TMPDIR_TEST/in.txt"
printf 'line1\nline2\n' > "$IN"
check "input redirection"  "wc -l < $IN"  "2"

echo ""
echo "--- Background jobs ---"
check "background job syntax" "echo fg ; sleep 0 &" "fg"

echo ""
echo "--- Variable assignment and expansion ---"
check "simple var assign+expand" 'X=world ; echo $X'          "world"
check "var in pipeline"          'V=hello ; echo $V | cat'    "hello"
check "HOME expansion"           'echo $HOME'                 "$HOME"
check "undefined var is empty"   'echo pre$UNDEFINED_XYZ end' "pre end"

echo ""
echo "--- Quoted strings ---"
check "double-quoted arg"   'echo "hello world"'           "hello world"
check "quoted with spaces"  'echo "one two three" | wc -w' "3"

echo ""
echo "--- Parse-only checks (AST printer) ---"
check_parse "simple command"    "ls -l"
check_parse "pipeline"          "cat /etc/hostname | wc -c"
check_parse "input redirect"    "sort < /dev/null"
check_parse "output redirect"   "echo x > /dev/null"
check_parse "in+out redirect"   "sort < /dev/null > /dev/null"
check_parse "background"        "sleep 5 &"
check_parse "multiple jobs"     "echo a ; echo b ; echo c"
check_parse "var reference"     'echo $HOME'
check_parse "var assign token"  "MY_VAR=value"

echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="
[ "$FAIL" -eq 0 ]
