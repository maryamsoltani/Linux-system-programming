# AGENT.md — AiShell Week 05: Process Management

This file is written for an AI assistant working on this project.
Read it before writing, refactoring, or extending any code here.

---

## What this project is

Week 05 extends the AiShell project into a real interactive Unix shell.
The shell runs a REPL (Read-Eval-Print Loop), distinguishes built-in commands
from external programs, and uses `fork()` + `execvp()` to create a new process
for every external command.

Key additions over week 04:
- A prompt loop that reads user input and dispatches commands
- `fork()` / `execvp()` / `waitpid()` for external command execution
- Background execution via trailing `&`
- Multi-command pipelines via `|` using `pipe()` and `dup2()`
- Signal handling: `SIGINT` (Ctrl+C) and `SIGCHLD` (background job cleanup)

---

## Repository layout

```
week05/
├── shell.h       shared types (Command, Pipeline) and all function declarations
├── shell.c       REPL loop + command-line parser
├── builtins.c    built-in command implementations (cd, pwd, exit, help)
├── execute.c     fork/exec for single commands and pipelines
├── signals.c     SIGINT and SIGCHLD signal handlers
└── Makefile
```

---

## Key data structures

### Command

Represents one parsed command with its arguments.

```c
typedef struct {
    char *argv[MAX_ARGS];   // argv[0] = program name, NULL-terminated
    int   argc;             // number of arguments
    int   background;       // 1 if user appended '&'
} Command;
```

### Pipeline

Represents one full input line, which may contain multiple commands chained by `|`.

```c
typedef struct {
    Command cmds[MAX_CMDS]; // array of commands
    int     num_cmds;       // how many commands are in the pipeline
    int     background;     // 1 if the last command had '&'
} Pipeline;
```

---

## How the REPL works (shell.c)

```
while (1):
    print prompt "aishell> "
    read line with fgets()
    parse line into Pipeline
        → manually split on '|' to get segments
        → tokenize each segment with strtok() into a Command
        → detect trailing '&' and set background flag
    if single command and it is a built-in:
        run_builtin()
    else:
        run_pipeline()
```

### Important: strtok is not reentrant

`strtok()` stores its position in a hidden global pointer. You cannot use it
at two levels of parsing at the same time. In `parse_pipeline()` we manually
scan for `|` and null-terminate each segment first, then call `strtok()` only
inside `parse_segment()`. Never switch back to using `strtok()` for the outer
split — it will silently break pipeline parsing.

---

## How execute.c works

### Single command — `launch()`

```
fork()
  child:  restore SIGINT to SIG_DFL, then execvp()
  parent: if foreground → waitpid() until child exits
          if background → print PID, return immediately
```

### Pipeline — `run_pipeline()`

For N commands the algorithm is:

```
infd = STDIN_FILENO

for i = 0 to N-1:
    if not last command: pipe(fd)   // fd[0]=read, fd[1]=write
    fork()
      child:
        restore SIGINT to SIG_DFL
        if infd != stdin: dup2(infd, stdin)   // read from previous pipe
        if not last:      dup2(fd[1], stdout) // write to next pipe
        execvp()
      parent:
        close fd[1]            // parent does not write to this pipe
        infd = fd[0]           // next iteration reads from here

if foreground: waitpid() for all children
```

The key rule: always close the write end of a pipe in the parent. If you do
not close it, the next command will block waiting for EOF that never comes.

---

## How signals.c works

### SIGCHLD handler

Called automatically by the kernel when any child process changes state.
Uses `waitpid(-1, &status, WNOHANG)` in a loop to reap all children that
have already exited without blocking. Prints `[done] PID X` for each.
This prevents zombie processes from accumulating.

### SIGINT handler (shell process)

The shell itself does not terminate on Ctrl+C. The handler just prints a
newline and returns, which brings the user back to a clean prompt.

### SIGINT in child processes

Every child process (in both `launch()` and `run_pipeline()`) calls
`signal(SIGINT, SIG_DFL)` before `execvp()`. This restores the default
behaviour so that Ctrl+C actually kills the foreground child.

`setup_signals()` must be called once at the start of `main()` before the
REPL loop begins.

---

## Adding a new built-in command

1. Write the implementation function in `builtins.c`:
   ```c
   static int builtin_myname(Command *cmd) { ... }
   ```
2. Add `"myname"` to the `BUILTINS[]` string array in `builtins.c`.
3. Add a branch in `run_builtin()`:
   ```c
   if (strcmp(cmd->argv[0], "myname") == 0) return builtin_myname(cmd);
   ```
4. Run `make` — zero warnings is the goal.

No changes needed in `shell.h`, `shell.c`, or `execute.c`.

---

## Guidelines for AI assistance on this project

### Set context clearly

When starting a task, state:
- Whether you are adding a built-in or changing process/signal behaviour.
- Target environment: Linux, GCC, POSIX (`unistd.h`, `sys/wait.h`, `signal.h`).
- Constraints: C99/C11, no external libraries, keep each file focused on one responsibility.

### When adding pipe features (redirection, multi-pipe)

- `dup2(src, dst)` duplicates `src` onto file descriptor `dst`.
  Always `close(src)` after `dup2` — the original fd is no longer needed.
- Close both ends of a pipe in any process that does not use them.
  An unclosed write end keeps the read end open forever (reader blocks).
- Test with at least a two-command and a three-command pipeline after every change.

### When changing signal handling

- Never call `printf` inside a signal handler in production code — it is not
  async-signal-safe. For this course project it is acceptable and kept simple.
- Always restore `SIGINT` to `SIG_DFL` in the child before `execvp()`.
  If you forget, Ctrl+C will have no effect on the running command.
- `SIGCHLD` with `WNOHANG` must loop until `waitpid` returns 0 or -1,
  because multiple children can exit before the handler runs once.

### When debugging a build error

- Read the full GCC error — file name and line number are always shown.
- Common mistakes: forgetting to add a new `.c` file to `SRCS` in the Makefile,
  declaring a function in `shell.h` but not implementing it, closing a file
  descriptor twice (causes silent data loss in pipes).

### What not to do

- Do not use `strtok()` at two levels of parsing simultaneously — use manual
  scanning for the outer split and `strtok()` only for the inner tokenization.
- Do not call `waitpid()` for background processes in the foreground wait loop —
  `SIGCHLD` handles reaping them.
- Do not share pipe file descriptors across unrelated commands — always close
  unused ends immediately after `fork()`.
- Do not add error handling for impossible cases — trust that `fork()` succeeds
  in a course environment; only handle `execvp()` failure (command not found).

---

## Quick reference — file to touch for each task

| Task | Files to change |
|---|---|
| Add a new built-in command | `builtins.c` only |
| Add input/output redirection (`<`, `>`) | `shell.c` (parser), `execute.c` |
| Add job control (`fg`, `bg`, `jobs`) | `builtins.c`, `signals.c`, `shell.h` |
| Change the prompt string | `shell.c` — `printf("aishell> ")` |
| Add a new signal handler | `signals.c`, declare in `shell.h` |
| Change pipeline limit | `shell.h` — `MAX_CMDS` constant |
| Change argument limit | `shell.h` — `MAX_ARGS` constant |
| After editing `shell.h` | `make clean && make` (required) |
