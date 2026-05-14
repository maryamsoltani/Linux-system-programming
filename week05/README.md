# Week 05 — Process Management Shell

A small interactive Unix shell written in C that demonstrates process management concepts: creating processes, running commands in the foreground and background, chaining commands with pipes, and handling signals.

---

## Goals

- Run external commands by creating a new process for each one
- Support background execution with `&`
- Distinguish between built-in commands and external programs
- Chain commands together using pipes (`|`)
- Handle signals so Ctrl+C kills the running command without exiting the shell

---

## How to Build

```bash
make
```

To clean and rebuild:

```bash
make clean && make
```

---

## How to Run

```bash
./aishell
```

You will see the prompt:

```
aishell>
```

---

## Built-in Commands

These run directly inside the shell process — no fork is created.

| Command | Description |
|---|---|
| `cd [dir]` | Change directory |
| `pwd` | Print current directory |
| `help` | List available built-in commands |
| `exit` | Quit the shell |

---

## External Commands

Any command that is not a built-in is run as an external process using `fork()` and `execvp()`. The shell waits for the process to finish before showing the prompt again.

```
aishell> ls -l
aishell> echo hello world
aishell> cat file.txt
```

---

## Background Execution

Add `&` at the end of a command to run it in the background. The shell prints the PID and returns to the prompt immediately. When the background process finishes, the shell prints `[done] PID`.

```
aishell> sleep 5 &
[background pipeline] 1 commands started

aishell> ls
...
[done] PID 12345
```

---

## Pipes

Commands can be chained with `|`. The output of each command becomes the input of the next. Any number of pipes can be used in one line.

```
aishell> ls | grep .c
aishell> ls | grep .c | wc -l
aishell> echo hello world | tr a-z A-Z
```

---

## Signal Handling

| Signal | Behavior |
|---|---|
| `Ctrl+C` (SIGINT) | Kills the currently running foreground command and returns to the prompt. The shell itself is not terminated. |
| SIGCHLD | When a background process finishes, the shell automatically reaps it and prints `[done] PID`. This prevents zombie processes. |

---

## Project Structure

```
week05/
├── shell.h       # Shared types (Command, Pipeline) and declarations
├── shell.c       # REPL loop and command parser
├── builtins.c    # Built-in command implementations
├── execute.c     # fork/exec for external commands and pipe execution
├── signals.c     # SIGINT and SIGCHLD signal handlers
└── Makefile
```
