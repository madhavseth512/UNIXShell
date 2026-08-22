#!/bin/bash
# Valgrind every code path that allocates: parse, exec, redirection, pipes,
# $? rewriting, syntax errors, and both exit routes.
cd ~/projects/msh || exit 1
rm -rf /tmp/mshmem && mkdir -p /tmp/mshmem

check() {
    local name="$1" script="$2"
    local log=/tmp/mshmem/log
    printf '%b' "$script" | valgrind --leak-check=full --show-leak-kinds=all \
        --track-origins=yes --error-exitcode=99 ./msh >/dev/null 2>"$log"
    local rc=$?
    # A child that fails to exec calls _exit() with the parent's heap still
    # mapped, so "still reachable" is expected there. Only lost bytes and
    # actual errors are defects; the shell process itself must reach 0.
    local lost errs shell_heap
    lost=$(grep -oE '(definitely|indirectly|possibly) lost: [0-9,]+ bytes' "$log" \
           | grep -v ': 0 bytes' | head -3)
    errs=$(grep -oE 'ERROR SUMMARY: [0-9]+ errors' "$log" | grep -v ': 0 errors' | head -1)
    shell_heap=$(grep -o 'in use at exit: [0-9,]* bytes' "$log" | tail -1)
    if [ "$rc" = 99 ] || [ -n "$lost" ] || [ -n "$errs" ] || \
       [ "$shell_heap" != "in use at exit: 0 bytes" ]; then
        printf 'FAIL %-28s shell heap %s\n' "$name" "$shell_heap"
        [ -n "$lost" ] && printf '       lost: %s\n' "$lost"
        [ -n "$errs" ] && printf '       %s\n' "$errs"
        grep -E 'Invalid (read|write|free)' "$log" | head -3
        return 1
    fi
    printf 'ok   %-28s shell heap %s, 0 lost, 0 errors\n' "$name" "$shell_heap"
}

fails=0
check "simple command"        'echo hi\n' || fails=$((fails+1))
check "command not found"     'nosuchcmd\n' || fails=$((fails+1))
check "builtins"              'cd /tmp\npwd\n' || fails=$((fails+1))
check "redirection"           'echo a > /tmp/mshmem/x\ncat < /tmp/mshmem/x\n' || fails=$((fails+1))
check "append + bare >"       'echo a >> /tmp/mshmem/y\n> /tmp/mshmem/z\n' || fails=$((fails+1))
check "failed redirect"       'cat < /no/such\n' || fails=$((fails+1))
check "two-stage pipe"        'echo hi | cat\n' || fails=$((fails+1))
check "four-stage pipe"       'echo hi | cat | cat | cat\n' || fails=$((fails+1))
check "and-or chain"          'false && echo a || echo b\n' || fails=$((fails+1))
check "semicolon list"        'echo a; echo b; echo c\n' || fails=$((fails+1))
check "\$? rewriting"          'false\necho code=$?-$?\n' || fails=$((fails+1))
check "syntax error"          '| bad\necho a &&\n' || fails=$((fails+1))
check "lexer error"           'echo a & echo b\n' || fails=$((fails+1))
check "exit builtin"          'false\nexit $?\n' || fails=$((fails+1))
check "exit mid-list"         'echo a; exit 3; echo b\n' || fails=$((fails+1))
check "eof exit"              'echo a\n' || fails=$((fails+1))
check "long line"             "echo $(printf 'x%.0s' {1..5000})\n" || fails=$((fails+1))
check "many args"             "echo $(seq 1 500 | tr '\n' ' ')\n" || fails=$((fails+1))
check "everything at once"    'cd /tmp; pwd > /tmp/mshmem/w && cat < /tmp/mshmem/w | cat || echo no; echo $?\n' || fails=$((fails+1))

echo
echo "valgrind failures: $fails"
[ "$fails" -eq 0 ]
