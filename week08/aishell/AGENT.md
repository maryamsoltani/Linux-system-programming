# AGENT.md — AiShell Week 08: Socket Client + MCP Server

This file is written for an AI assistant working on this codebase.
Read it before writing, refactoring, or extending any code here.

See also `../AGENT.md` for the full week08 project (aishell + MCP server + agent notebook).

---

## What this project is

Week 08 extends the Week 06 BusyBox-inspired shell (`aishell`) with:
- **`net-get`** — TCP socket client command (`socket→connect→send/recv→close`)
- **`mcp_server`** — fork-per-client JSON tool server on TCP port 9000

The original shell (`aishell`) contains many Unix-style commands dispatched
through a shared registry. It demonstrates:

- A command registry pattern (register → find → dispatch)
- Process creation and IPC: `fork()`, `execvp()`, `pipe()`, `dup2()`, `waitpid()`
- POSIX threads: `pthread_create()`, `pthread_join()`, `pthread_mutex_t`
- Raw-mode terminal input: `tcsetattr()` with `ICANON | ECHO` disabled
- A natural-language `@` interface with deterministic fallback and optional LLM helper

---

## Repository layout

```
week08/aishell/
├── main.c                    REPL, line editor, history, tab completion,
│                             pipeline dispatch, @ NL interface
├── cmd_spec.h                cmd_spec_t typedef — the contract every command must fulfill
├── registry.c                static array of cmd_spec_t*, register/find/iterate
├── register_all_commands.c   calls register_*_command() for every built-in
├── json_utils.h / .c         json_print_string(), json_print_escaped_char()
├── cmd_netget.h / .c         NEW — net-get TCP socket client command
├── mcp_server.c              NEW — JSON tool server, fork-per-client, port 9000
└── cmd_<name>.h / .c         23 original built-in commands
```

### Week 08 new files

| File | Purpose |
|------|---------|
| `cmd_netget.c` | HTTP GET via raw socket; flags: `--port`, `--path`, `--timeout`, `--headers`, `--json` |
| `cmd_netget.h` | Header declaring `netget_run`, `netget_print_usage`, `register_netget_command` |
| `mcp_server.c` | Stand-alone binary: accept loop → fork → parse JSON → dispatch command → return JSON |

---

## Key types

### cmd_spec_t (cmd_spec.h)

```c
typedef struct cmd_spec {
    const char *name;
    const char *summary;
    const char *long_help;
    int  (*run)(int argc, char **argv);
    void (*print_usage)(FILE *out);
} cmd_spec_t;
```

Every command module defines one `cmd_spec_t` and one `register_*_command()`
function that calls `register_command(&cmd_*_spec)`. Nothing else needs to
know about the module.

---

## How dispatch works (main.c)

```
read input line
if line starts with '@'  → handle_natural_language_request()
if line contains '|'     → dispatch_pipeline_line()
else                     → split_line() → dispatch_command()

dispatch_command():
  argv[0] == "exit"/"quit" → return -1 (shell exits)
  argv[0] == "help"        → print_shell_help() or per-command help
  argv[0] == "--version"   → print_shell_version()
  else                     → find_command(argv[0]) → cmd->run(argc, argv)
```

### Pipeline execution

```
for each segment split on '|':
    pipe(fd)
    fork()
      child:  dup2() to wire stdin/stdout, then execvp(aishell, segment_argv)
      parent: close write end, save read end for next segment
waitpid() for all children
```

Each pipeline stage re-executes the `aishell` binary itself with one command
segment. This means the child process goes through the same `main()` entry
point and dispatches the single command directly (no interactive shell loop).

---

## How the line editor works (main.c)

`read_line_raw()` puts the terminal in raw mode with `tcsetattr()`:
- `ICANON` disabled: input is available character-by-character
- `ECHO` disabled: characters are not automatically echoed

Key sequences handled:

| Input | Action |
|---|---|
| `\n` / `\r` | Accept line, restore terminal |
| `Ctrl-D` (0x04) on empty line | Exit shell |
| `127` / `8` | Backspace — `memmove` in buffer |
| `\t` | `complete_current_word()` |
| `ESC [ A` | Up arrow — history older |
| `ESC [ B` | Down arrow — history newer |
| `ESC [ C` | Right arrow — cursor forward |
| `ESC [ D` | Left arrow — cursor backward |
| printable | Insert at cursor position with `memmove` |

`refresh_input_line()` reprints the line using `\r` + `\033[K` (erase to end).

---

## How the @ interface works (main.c)

```
handle_natural_language_request(line):
    request = line + 1  (skip '@')
    if cached → show cached suggestion
    clarify_missing_nl_arguments()   // ask for dir name if missing, etc.
    fallback_nl_to_command()         // deterministic keyword matching
    if no match → read_helper_command()   // call MYSH_LLM_HELPER if set
    validate: command_is_safe_to_dispatch()
    cache the translation
    show "AI suggestion: <cmd>"
    if interactive → ask "Run it? [y/N]"
    if rejected and deterministic → try Ollama for alternate suggestion
```

### Safety validation

`command_is_safe_to_dispatch()` rejects any suggestion that contains
`;`, `&`, `|`, `<`, `>`, `` ` ``, `$`, `(`, `)`, or newlines, then
checks that `argv[0]` is a registered command or a known built-in.
This prevents the LLM from suggesting arbitrary shell injection.

---

## How threads work (cmd_threads.c)

Each worker receives a `struct thread_job` containing its 0-based index,
a sleep duration, a pointer to the shared sum, and a pointer to the mutex.

```c
// worker computes (index+1)^2, locks mutex, adds to shared sum
job->result = (long)(job->index + 1) * (long)(job->index + 1);
pthread_mutex_lock(job->sum_lock);
*job->shared_sum += job->result;
pthread_mutex_unlock(job->sum_lock);
```

`pthread_create()` is called for all workers first, then `pthread_join()`
waits for each in order. Results are printed after all threads finish.

---

## How the package manager works (cmd_pkg.c)

| Subcommand | What it does |
|---|---|
| `pkg build <src> <out.tar.gz>` | `fork` + `execvp("tar", ["-czf", out, "-C", src, "."])` |
| `pkg install <tar.gz>` | `mkdtemp` → extract → read `pkg.json` → `rename` to `~/.mysh/pkgs/<name>-<ver>/` → `symlink` executables into `~/.mysh/bin/` → append to `pkgdb.txt` |
| `pkg list` | Read and print `~/.mysh/pkgdb.txt` |
| `pkg remove <name>` | Look up install dir in `pkgdb.txt` → remove symlinks → `rm -rf` install dir → rewrite `pkgdb.txt` without that entry |

`pkg.json` is parsed with simple `strstr` pattern matching — no JSON library.
It must contain `"name"`, `"version"`, and `"files"` keys.

---

## Makefile targets (week08)

| Target | Binary | Sources |
|--------|--------|---------|
| `aishell` | interactive shell | `main.c` + all `cmd_*.c` |
| `mcp_server` | JSON tool server | `mcp_server.c` + all `cmd_*.c` |

When adding a new command, add `cmd_myname.c` to **both** `SRC` and `MCP_SRC`
in the Makefile so both binaries include it.

---

## Adding a new command

1. Create `cmd_myname.h`:
   ```c
   #ifndef CMD_MYNAME_H
   #define CMD_MYNAME_H
   #include <stdio.h>
   int myname_run(int argc, char **argv);
   void myname_print_usage(FILE *out);
   #endif
   ```

2. Create `cmd_myname.c` with `myname_run()`, `myname_print_usage()`,
   a `cmd_spec_t cmd_myname_spec = { ... }`, and:
   ```c
   void register_myname_command(void) {
       register_command(&cmd_myname_spec);
   }
   ```

3. Declare `void register_myname_command(void);` in `register_all_commands.c`
   and call it inside `register_all_builtin_commands()`.

4. Add `cmd_myname.c` to `SRC` in the `Makefile`.

5. `make` — zero warnings is the goal.

---

## Adding a new `@` rule

`fallback_nl_to_command()` in `main.c` maps keywords to commands. To add a
new rule, add an `else if` branch before the final `return 0`:

```c
} else if (text_contains(lower, "my keyword")) {
    snprintf(command, command_size, "mycommand");
}
```

Use `text_contains(lower, ...)` for case-insensitive matching (the request is
already lowercased into `lower`). Use `extract_last_name()` if the command
needs a filename argument extracted from the request.

---

## Guidelines for AI assistance on this project

### Argument parsing

All commands parse `argc`/`argv` manually — no argtable3. The standard pattern is:

```c
for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
        myname_print_usage(stdout); return 0;
    } else if (strcmp(argv[i], "--json") == 0) {
        json = 1;
    } else if (argv[i][0] == '-') {
        fprintf(stderr, "myname: invalid option: %s\n", argv[i]);
        myname_print_usage(stderr); return 1;
    } else {
        /* positional argument */
    }
}
```

### JSON output

Use `json_print_string(stdout, value)` for any string value. Never build JSON
with `printf` string formatting for user-controlled content — it will break on
quotes, backslashes, and control characters.

### Terminal raw mode

`read_line_raw()` owns the terminal state. Do not call `tcsetattr()` anywhere
else. If a command needs to prompt for input (e.g. the `@` interface asking
`Run it? [y/N]`), restore canonical mode with `tcsetattr(STDIN_FILENO,
TCSAFLUSH, &original)` before reading, then re-apply raw mode afterward.

### Pipeline / fork rules

- Always close unused pipe ends in the parent after `fork()`. An unclosed
  write end keeps the reader blocked indefinitely.
- Each child calls `execvp(shell_program_path, ...)` to re-execute `aishell`
  with one command segment. Do not call `dispatch_command()` directly in the
  child — the child must `exec` so it gets a clean process image.
- After all `fork()` calls, close all pipe ends in the parent, then
  `waitpid()` for every child.

### What not to do

- Do not call `exit()` from inside a command's `run()` function — return a
  non-zero status code instead. `exit()` in a command would kill the whole shell.
- Do not use global mutable state in command modules. The registry and JSON
  helpers are the only shared globals.
- Do not add argtable3 — keep the project dependency-free.
- Do not use `strtok()` in the line editor or pipeline splitter — they operate
  on the same buffer and `strtok()` is not reentrant.

---

## Quick reference — file to touch for each task

| Task | Files to change |
|---|---|
| Add a new command | `cmd_<name>.h`, `cmd_<name>.c`, `register_all_commands.c`, `Makefile` |
| Add a new `@` keyword rule | `main.c` — `fallback_nl_to_command()` |
| Change the prompt string | `main.c` — `"aishell> "` literals |
| Change history file location | `main.c` — `HISTORY_FILE` constant |
| Change max pipeline depth | `main.c` — `MAX_PIPE_COMMANDS` constant |
| Change max command registry size | `registry.c` — `MAX_COMMANDS` constant |
| Add JSON to an existing command | `cmd_<name>.c` — check `--json` flag and use `json_utils.h` |
| After editing `cmd_spec.h` | `make clean && make` (required) |
