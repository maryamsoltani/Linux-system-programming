#!/bin/sh
# test.sh — smoke tests for mish.
#
# Each test pipes a scripted input into the shell and greps the output
# for the expected string.  Exits non-zero if any test fails.

set -u
cd "$(dirname "$0")/.."

make >/dev/null

MISH=./mish
PASS=0
FAIL=0
TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

run () {
    name="$1"; input="$2"; expected="$3"
    out=$(printf '%s' "$input" | $MISH 2>&1 || true)
    if printf '%s' "$out" | grep -qF -- "$expected"; then
        printf '  ok    %s\n' "$name"
        PASS=$((PASS + 1))
    else
        printf '  FAIL  %s\n' "$name"
        printf '        expected to find: %s\n' "$expected"
        printf '        actual output:\n%s\n' "$out" | sed 's/^/          /'
        FAIL=$((FAIL + 1))
    fi
}

printf 'mish smoke tests\n'

# ---------- builtins ----------
run "pwd built-in" \
    'pwd
exit
' \
    "$(pwd)"

run "cd built-in" \
    "cd $TMPDIR
pwd
exit
" \
    "$TMPDIR"

run "prompt built-in" \
    'prompt foo$
exit
' \
    'foo$'

run "help built-in lists exit" \
    'help
exit
' \
    "exit"

# ---------- external commands ----------
run "external echo" \
    'echo hello world
exit
' \
    "hello world"

run "external command not found" \
    'this_does_not_exist_xyz
exit
' \
    "this_does_not_exist_xyz"

# ---------- redirection ----------
run "output redirection >" \
    "echo saved > $TMPDIR/out.txt
cat $TMPDIR/out.txt
exit
" \
    "saved"

run "output append redirection >>" \
    "echo line1 > $TMPDIR/app.txt
echo line2 >> $TMPDIR/app.txt
cat $TMPDIR/app.txt
exit
" \
    "line2"

run "input redirection <" \
    "echo hello > $TMPDIR/in.txt
wc -c < $TMPDIR/in.txt
exit
" \
    "6"

# ---------- pipelines ----------
run "two-stage pipe" \
    'printf hello | wc -c
exit
' \
    "5"

run "three-stage pipe" \
    "ls $TMPDIR | wc -l | wc -c
exit
" \
    "2"

# ---------- separators ----------
run "sequential ;" \
    'echo first ; echo second
exit
' \
    "second"

run "syntax error: leading separator" \
    '| echo hi
exit
' \
    "syntax error"

run "syntax error: missing redirection target" \
    'echo hi >
exit
' \
    "syntax error"

# ---------- wildcards ----------
run "wildcard expansion *.c" \
    'cd src
ls *.c | wc -l
exit
' \
    "4"

# ---------- background ----------
# Background jobs return to prompt immediately; we just want to make sure
# the shell doesn't hang or crash.
run "background &" \
    'sleep 1 &
echo done
exit
' \
    "done"

printf '\n%d passed, %d failed\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ]
