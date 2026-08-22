#!/bin/bash
# Behavioural test suite for msh.
#   run  <name> <expected-stdout>        <script>   (stderr discarded)
#   both <name> <expected-stdout+stderr> <script>   (streams merged)
#   code <name> <expected-exit>          <script>
# Both <expected> and <script> are passed through printf %b, so use \n.
cd ~/projects/msh || exit 1

pass=0; fail=0
check() {
    local name="$1" expect="$2" got="$3"
    if [ "$got" = "$expect" ]; then
        pass=$((pass+1)); printf 'ok   %s\n' "$name"
    else
        fail=$((fail+1))
        printf 'FAIL %s\n       expected: %q\n       got:      %q\n' \
               "$name" "$expect" "$got"
    fi
}
run()  { check "$1" "$(printf '%b' "$2")" "$(printf '%b' "$3" | ./msh 2>/dev/null | tr -d '\r')"; }
both() { check "$1" "$(printf '%b' "$2")" "$(printf '%b' "$3" | ./msh 2>&1     | tr -d '\r')"; }
code() {
    printf '%b' "$3" | ./msh >/dev/null 2>&1
    check "$1" "$2" "$?"
}

rm -rf /tmp/mshtest && mkdir -p /tmp/mshtest
printf 'kill -TERM $$\n'   > /tmp/mshtest/selfkill.sh
printf 'a\nb\nc\n'         > /tmp/mshtest/abc
: > /tmp/mshtest/ne && chmod -x /tmp/mshtest/ne

echo "--- milestone 2: words, exec, exit codes ---"
run  "simple"             "hello world"  'echo hello world\n'
run  "extra whitespace"   "a b"          'echo    a     b\n'
run  "blank lines"        "x"            '\n\n   \necho x\n'
run  "no trailing nl"     "tail"         'echo tail'
both "not found message"  "msh: nope: No such file or directory" 'nope\n'
run  "not found -> 127"   "127"          'nope\necho $?\n'
run  "not exec -> 126"    "126"          '/tmp/mshtest/ne\necho $?\n'
run  "signal -> 128+n"    "143"          'sh /tmp/mshtest/selfkill.sh\necho $?\n'

echo "--- milestone 3: builtins ---"
run  "pwd"                "/tmp"         'cd /tmp\npwd\n'
run  "cd twice"           "/etc"         'cd /tmp\ncd /etc\npwd\n'
run  "cd no arg -> HOME"  "$HOME"        'cd /tmp\ncd\npwd\n'
both "cd failure message" "msh: cd: /no/such: No such file or directory" 'cd /no/such\n'
run  "failed cd sets 1"   "1"            'true\ncd /no/such\necho $?\n'
run  "good cd sets 0"     "0"            'false\ncd /tmp\necho $?\n'
run  "pwd sets 0"         "0"            'false\npwd > /dev/null\necho $?\n'
code "exit 42"            42             'exit 42\n'
code "exit no arg"        1              'false\nexit\n'
code "exit bad arg"       2              'exit abc\n'
code "eof uses last"      1              'false\n'
run  "exit stops rest"    "before"       'echo before\nexit\necho after\n'

echo "--- \$? expansion ---"
run  "bare"               "1"            'false\necho $?\n'
run  "embedded in word"   "code=1"       'false\necho code=$?\n'
run  "twice in one word"  "1-1"          'false\necho $?-$?\n'
run  "expanded at runtime" "0\n1"        'true\necho $?; false; echo $?\n'

echo "--- milestone 4: redirection ---"
run  "> then <"           "into a file"  'echo into a file > /tmp/mshtest/a\ncat < /tmp/mshtest/a\n'
run  ">> appends"         "one\ntwo"     'echo one > /tmp/mshtest/b\necho two >> /tmp/mshtest/b\ncat /tmp/mshtest/b\n'
run  "> truncates"        "second"       'echo first > /tmp/mshtest/c\necho second > /tmp/mshtest/c\ncat /tmp/mshtest/c\n'
run  "no spaces needed"   "tight"        'echo tight>/tmp/mshtest/d\ncat</tmp/mshtest/d\n'
run  "builtin redirected" "/tmp"         'cd /tmp\npwd > /tmp/mshtest/e\ncat /tmp/mshtest/e\n'
run  "shell fds restored" "still here"   'pwd > /tmp/mshtest/f\necho still here\n'
both "open failure msg"   "msh: /no/such/f: No such file or directory" 'cat < /no/such/f\n'
run  "open failure -> 1"  "1"            'cat < /no/such/f\necho $?\n'
run  "bare > creates"     "ok"           '> /tmp/mshtest/g\ncat /tmp/mshtest/g\necho ok\n'
run  "redirect beats pipe" "in-file"     'echo in-file | cat > /tmp/mshtest/i\ncat /tmp/mshtest/i\n'

echo "--- milestone 5: pipes ---"
run  "two stage"          "hello"        'echo hello | cat\n'
run  "three stage"        "3"            'cat /tmp/mshtest/abc | cat | wc -l\n'
run  "four stage"         "C"            'cat /tmp/mshtest/abc | cat | tr a-z A-Z | tail -1\n'
run  "status is last (0)" "0"            'false | true\necho $?\n'
run  "status is last (1)" "1"            'true | false\necho $?\n'
run  "reader exits early" "ok"           'yes | head -1 > /dev/null\necho ok\n'
run  "builtin in a pipe"  "/tmp"         'cd /tmp\npwd | cat\n'
run  "input redir + pipe" "3"            'cat < /tmp/mshtest/abc | wc -l\n'

echo "--- milestone 6: ; && || ---"
run  "semicolon"          "a\nb"         'echo a; echo b\n'
run  "trailing semicolon" "a"            'echo a;\n'
run  "&& runs on 0"       "yes"          'true && echo yes\n'
run  "&& skips on 1"      ""             'false && echo no\n'
run  "|| skips on 0"      ""             'true || echo no\n'
run  "|| runs on 1"       "yes"          'false || echo yes\n'
run  "false && a || b"    "b"            'false && echo a || echo b\n'
run  "left associative"   "a\nb"         'true && echo a && echo b\n'
run  "mixed with ;"       "x\nz"         'echo x; false && echo y; echo z\n'
run  "pipeline in &&"     "hi"           'echo hi | cat && true\n'
run  "|| sees pipe status" "no"          'true | false || echo no\n'
run  "status after skip"  "1"            'false && echo a\necho $?\n'

echo "--- syntax errors ---"
both "leading pipe"       "msh: syntax error near unexpected token \`|'" '| echo\n'
both "dangling &&"        "msh: syntax error: unexpected end of input"   'echo a &&\n'
both "redirect no target" "msh: syntax error: unexpected end of input"   'echo a >\n'
both "bare ampersand"     "msh: syntax error near unexpected token \`&'" 'echo a & echo b\n'
both "double semicolon"   "msh: syntax error near unexpected token \`;'" 'echo a ;; echo b\n'
run  "syntax error -> 2"  "2"            'echo a &&\necho $?\n'
run  "shell survives it"  "alive"        '| bad\necho alive\n'
run  "nothing executed"   ""             'echo hi &&\n'

echo
echo "passed: $pass   failed: $fail"
[ "$fail" -eq 0 ]
