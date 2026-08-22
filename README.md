# msh

A UNIX shell written from scratch in C11, using only POSIX.1-2008 interfaces.

```sh
make && ./msh
```

## What it does

```sh
msh> echo hello | tr a-z A-Z          # pipelines, any number of stages
msh> sort < input.txt > sorted.txt    # < > >> redirection
msh> make && ./run || echo failed     # && || ; with real short-circuiting
msh> cd /tmp; pwd                     # builtins that run in the shell itself
msh> false; echo $?                   # exit statuses, 127/126/128+n conventions
msh> sleep 30                         # Ctrl-C kills the child, not the shell
```

## Implementation

| File | Role |
|------|------|
| `lexer.c` | character scanner producing typed tokens |
| `parser.c` | recursive descent into an AST |
| `exec.c` | pipelines, redirection, `&&`/`\|\|`, `$?` expansion |
| `builtins.c` | `cd`, `pwd`, `exit` |
| `signals.c` | `sigaction` setup for the shell and its children |
| `main.c` | REPL, terminal and process-group acquisition |

The grammar is:

```
list     := andor ( ';' andor )* [';']
andor    := pipeline ( ('&&' | '||') pipeline )*
pipeline := command ( '|' command )*
command  := ( WORD | redirect )+
redirect := ('<' | '>' | '>>') WORD
```

Each foreground pipeline is placed in its own process group, which is handed the
terminal with `tcsetpgrp` for the duration, so a Ctrl-C reaches the job and not the
shell.

## Tests

```sh
make test
```

- `tests/run.sh` — behavioural cases compared against `sh` semantics
- `tests/signals.py` — drives msh through a pty and sends a literal `0x03`
- `tests/pgroups.py` — verifies process-group isolation via `ps`
- `tests/memory.sh` — valgrind over every allocating path

## Not implemented yet

Quoting, `$VAR`, `~`, globbing, background jobs (`&`, `jobs`, `fg`, `bg`), history and
line editing.

## Building

Linux only. Requires `fork`, `execvp`, `waitpid`, `tcsetpgrp` and `termios`; it will not
build with a Windows toolchain. Flags are
`-std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wshadow -g`.
