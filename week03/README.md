# mish — a small UNIX shell

`mish` is a small UNIX-style shell in C. It reads a line from the user, parses it, and runs built-in commands or external programs — with pipes, redirection, wildcards, sequencing, and background execution.

This is a clean rewrite of the Week 3 shell exercise. Same feature surface as the original `Unix_Shell`, plus stricter compiler flags, better error messages, broader test coverage, append-redirect (`>>`), and arbitrary-length pipelines.

## Project Folder

```
mish/
├── src/             ← all C source
├── tests/           ← test script
├── Makefile
├── README.md
└── AGENT.md         ← deep walkthrough of the implementation
```

## Features

- Interactive prompt, `%` by default
- Built-in `prompt` command to change the prompt text
- Built-in `pwd` command to print the current working directory
- Built-in `cd` command to change directories (`cd` alone goes to `$HOME`)
- Built-in `exit` command to quit the shell
- Built-in `help` command to list all built-ins
- External command execution with `fork()` + `execvp()` (PATH lookup)
- Sequential command execution using `;`
- Background execution using `&`
- Pipelines using `|` — supports any number of stages, not just two
- Input redirection using `<`
- Output redirection using `>`
- **Output append redirection using `>>`**
- Wildcard expansion using `glob()` (`*`, `?`, `[…]`)
- Signal handling: shell ignores `Ctrl-C`, `Ctrl-\`, and `Ctrl-Z`; children get default behaviour
- Background child cleanup using `waitpid(..., WNOHANG)` from a SIGCHLD handler

## Build

```sh
make
```

Build flags default to `-Wall -Wextra -Wshadow -std=c11 -g`. Override with `make CFLAGS='-O2 -Wall'` if needed.

## Run

```sh
./mish
```

Example session:

```text
% pwd
/home/marmar/mish
% ls -l
% prompt shell$
shell$ echo hello
hello
shell$ echo greetings >> log.txt
shell$ cat log.txt | wc -l
1
shell$ sleep 5 &
shell$ exit
```

Press Ctrl-D at the prompt to exit cleanly without typing `exit`.

## Test

```sh
make test
```

The script (in `tests/test.sh`) builds the shell and pipes scripted input through it, verifying:

- All 5 built-ins (pwd, cd, prompt, help, exit)
- External command execution and "command not found" handling
- `>`, `>>`, and `<` redirection
- Two-stage and three-stage pipelines
- Sequential `;` execution
- Background `&` (smoke test only — full job-control isn't a goal here)
- Wildcard expansion
- Two syntax errors: leading separator, missing redirect target

16 tests, runs in under 3 seconds.

## Source Files

```text
src/mish.c       main loop, executor, signal handling
src/token.c      input tokenizer (handles | & ; < > >> as standalone tokens)
src/token.h
src/command.c    parser, redirection extraction, glob expansion
src/command.h    Command struct and separator constants
src/builtin.c    prompt, pwd, cd, exit, help
src/builtin.h
Makefile         build rules
tests/test.sh    smoke tests
AGENT.md         deep walkthrough — read this when extending mish
```

## What `mish` does NOT do

These are intentionally out of scope for the Week 3 spec:

- Quoted strings (`"..."` and `'...'` are passed literally to the program)
- Variable expansion (`$HOME`, `$?`, etc.)
- Tilde expansion (`~user`)
- Job control (no `jobs`, `bg`, `fg`)
- Command history or line editing
- `&&` and `||` short-circuit operators
- Here-documents (`<<`)
- Globbing inside redirection targets

If you want job control, see the sibling `jsh` project. If you want a full POSIX shell, see bash.
