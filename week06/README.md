# Week 06 — BusyBox-Style Shell (aishell)

A single-binary interactive shell written in C that bundles 23 Unix-style commands, demonstrates process pipelines, POSIX threads, raw-mode terminal line editing, and a natural-language `@` interface.

---

## Goals

- Package many commands into one binary dispatched through a shared registry
- Build pipelines with `fork()`, `pipe()`, `dup2()`, and `execvp()`
- Demonstrate POSIX threads with `pthread_create()`, `pthread_join()`, and a mutex
- Implement raw-mode terminal input with history, tab completion, and cursor movement
- Translate natural-language requests into shell commands via the `@` interface

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

Run a single command directly without entering the shell:

```bash
./aishell ls -l
./aishell localdate
./aishell threads -n 3
```

---

## Interactive Features

| Key | Action |
|---|---|
| `Up` / `Down` | Scroll through command history |
| `Left` / `Right` | Move cursor while editing |
| `Backspace` | Delete character before cursor |
| `Tab` | Complete command name or file path |
| `Ctrl-D` | Exit the shell |

History is saved between sessions in `~/.aishell_history`.

---

## Natural-Language `@` Interface

Start a line with `@` to describe what you want in plain English:

```
aishell> @ list files
AI suggestion: ls
Run it? [y/N]

aishell> @ show today's date
AI suggestion: localdate
Run it? [y/N]

aishell> @ create a directory named reports
AI suggestion: mkdir reports
Run it? [y/N]
```

The shell validates the suggestion before offering to run it. In non-interactive mode the suggestion is shown but not executed.

An external LLM helper can be plugged in via the `MYSH_LLM_HELPER` environment variable:

```bash
MYSH_LLM_HELPER="./ollama_llm_helper.sh" ./aishell
```

---

## Built-in Shell Commands

| Command | Description |
|---|---|
| `help` | Show all registered commands |
| `help <cmd>` | Show help for a specific command |
| `exit` / `quit` | Exit the shell |
| `--version` | Print shell version |

---

## Registered Commands

| Command | Description |
|---|---|
| `ls` | List directory contents |
| `cat` | Concatenate and print files |
| `echo` | Print text to standard output |
| `pwd` | Print working directory |
| `wc` | Count lines, words, and bytes |
| `head` | Print first lines of files |
| `tail` | Print last lines of files |
| `touch` | Create files or update timestamps |
| `mkdir` | Create directories |
| `rmdir` | Remove empty directories |
| `cp` | Copy files |
| `mv` | Move or rename files |
| `rm` | Remove files |
| `dirname` | Print directory portion of paths |
| `du` | Show disk usage |
| `whoami` | Print current username |
| `id` | Print user and group identifiers |
| `uname` | Print system information |
| `clear` | Clear the terminal screen |
| `localdate` | Print current local date |
| `procinfo` | Print process ID and parent process ID |
| `threads` | Run a POSIX threads demo |
| `pkg` | Manage shell packages |

Every command supports `--json` output and `-h` / `--help`.

---

## Pipelines

Commands can be chained with `|`. Each stage runs as a child process connected by pipes:

```bash
./aishell echo alpha beta gamma "|" wc -w
./aishell cat Makefile "|" head -n 5
./aishell ls "|" wc -l
```

Use quoted `"|"` when running from your system shell so the pipe token reaches `aishell` instead of being interpreted by the outer shell.

---

## Processes and Threads

### How pipelines work

```
parent aishell
  |
  +-- child 1: aishell <cmd1>   (stdout → pipe write end)
  +-- child 2: aishell <cmd2>   (stdin  ← pipe read end)
```

Each stage calls `fork()` → `dup2()` to rewire stdin/stdout → `execvp()` to re-execute `aishell` with one command segment.

### `procinfo` — see process IDs

```bash
./aishell procinfo
# pid=1234 ppid=1200

./aishell procinfo "|" wc -w
# each stage runs in its own child process
```

### `threads` — POSIX thread demo

```bash
./aishell threads -n 4
# started 4 threads
# thread 1 id ... result 1
# thread 2 id ... result 4
# thread 3 id ... result 9
# thread 4 id ... result 16
# sum 30
```

Each worker computes the square of its worker number. Results are accumulated into a shared sum protected by `pthread_mutex_lock()` / `pthread_mutex_unlock()`.

---

## Package Manager

The `pkg` command builds, installs, lists, and removes packages.

```bash
./aishell pkg build   <src-dir> <output-tar>
./aishell pkg install <tar-file>
./aishell pkg list
./aishell pkg remove  <name>
```

Packages install under `~/.mysh/pkgs/<name>-<version>/` with executable symlinks in `~/.mysh/bin/`.

---

## JSON Output

Every command supports `--json`:

```bash
./aishell ls --json
./aishell id --json
./aishell procinfo --json
./aishell threads --json -n 2
./aishell wc --json Makefile
./aishell help --json
./aishell help ls --json
```

---

## Project Structure

```
week06/
├── main.c                    interactive shell, line editing, pipelines, @ interface
├── cmd_spec.h                shared command interface (cmd_spec_t)
├── registry.c                command registry (register / find / iterate)
├── register_all_commands.c   calls register_*_command() for every built-in
├── json_utils.h / .c         JSON string escaping helpers
├── cmd_<name>.h / .c         one module per command (23 commands)
└── Makefile
```
