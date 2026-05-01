# AGENT.md — mish implementation guide

This is the deep walkthrough. Every file, every architectural decision, every subtle bit. Read this when you want to **understand** or **extend** `mish`. For everyday use, the README is enough.

---

## How a command line travels through mish

```
user input:  "ls *.c | grep token > out.txt &"
                        │
                        ▼
              ┌──────────────────┐
              │   tokenize()     │   token.c
              │ → ["ls","*.c",   │
              │    "|","grep",   │
              │    "token",">",  │
              │    "out.txt","&"]│
              └────────┬─────────┘
                       ▼
              ┌──────────────────┐
              │ parse_commands() │   command.c
              │ → 2 Command structs
              │   [0] argv=ls,*.c  sep=|
              │   [1] argv=grep,token
              │       stdout_file=out.txt
              │       sep=&
              └────────┬─────────┘
                       ▼
              ┌──────────────────┐
              │  execute_all()   │   mish.c
              │ groups by `|`:   │
              │ → run_pipeline(  │
              │     start=0,     │
              │     end=2,       │
              │     bg=1)        │
              └────────┬─────────┘
                       ▼
              ┌──────────────────┐
              │  run_pipeline    │   mish.c
              │  - alloc N-1 pipes│
              │  - fork each cmd │
              │  - wire fds      │
              │  - parent doesn't│
              │    wait (bg=1)   │
              └──────────────────┘
                       │
                       ▼
              SIGCHLD handler reaps later
```

The four boxes correspond to the four `.c` files. Each one is self-contained: token.c knows nothing about commands, command.c knows nothing about processes, mish.c knows nothing about parsing.

---

## File-by-file walkthrough

### `token.c` / `token.h`

The tokenizer mutates the input line in place. Word tokens are pointers into the line buffer; metacharacter tokens are pointers into a private static table. The line buffer must outlive the tokens — `mish.c` keeps it on the stack until `command_free` has run on every command.

The interesting bit is **metacharacter splitting without spaces**. `echo hi>out.txt` should tokenize identically to `echo hi > out.txt`. The strategy:

1. While reading a word, stop at whitespace **or** at any of `| & ; < >`.
2. If the word ended at whitespace, NUL-terminate normally.
3. If the word ended at a metacharacter, save the metachar, write NUL in its place, then emit the metachar token from the static table.

This is why metachar tokens point to a private `META_STR[]` table — the original character was overwritten by the NUL we wrote to terminate the preceding word.

`>>` gets a special case: when the lookahead shows two `>` in a row, we emit a single `">>"` token instead of two `">"` tokens. This is the only multi-character operator the shell supports.

### `command.c` / `command.h`

The parser's `Command` struct represents one program invocation:

```c
typedef struct Command {
    char  **argv;          // NULL-terminated, owned (must be freed)
    char   *stdin_file;    // points into the line buffer
    char   *stdout_file;   // points into the line buffer
    int     append_stdout; // 1 if >> was used
    char   *sep;           // SEP_PIPE / SEP_BG / SEP_SEQ
} Command;
```

`argv` strings are **strdup'd** because they need to outlive the original line buffer (glob expansion produces strings that aren't in the line at all). `stdin_file` / `stdout_file` are *not* strdup'd because they're always already in the line buffer — the parser's only consumer (the executor) finishes with them inside the same `eval` cycle.

`parse_commands()` runs three logical passes per call:

1. **Split** on `|`, `&`, `;`. Each span between separators becomes one Command. The separator goes in `cmd->sep`.
2. **Extract redirections** from each span. Walk the span's tokens; if you see `<`, `>`, or `>>`, take the next token as the target and mark both as "skip" in a parallel `keep[]` array.
3. **Build argv** from the surviving tokens with `glob()` for wildcard expansion. `GLOB_NOCHECK` means a pattern with no matches is preserved literally — `ls nonexistent*` should still try to ls the literal string and let ls produce the error.

Errors (consecutive separators, leading separator, missing redirect target, pipe at end of line) free any Commands already filled in, so the caller never has to clean up partially-parsed state on error. There's no half-success.

### `builtin.c` / `builtin.h`

Built-ins run **inside the shell process** so their side effects stick:

- `cd` changes the shell's cwd
- `prompt` mutates the global `g_prompt` string in `mish.c`
- `exit` returns 1 to signal "stop the REPL"

The dispatcher is a static `BUILTINS[]` table mapping name → function pointer + summary string. `builtin_is()` says whether a name is a builtin (used by the executor to decide whether to fork). `builtin_run()` calls the matching handler. `do_help()` walks the same table to print the list — there's only one place where built-in names are listed.

When a built-in appears in a pipeline (e.g., `pwd | cat`), the executor handles it inside the forked child. This means side effects don't reach the shell — `cd /tmp | cat` will not change the shell's cwd. That matches POSIX shell behaviour.

When a built-in has redirections but isn't in a pipeline (e.g., `pwd > out.txt`), the executor saves stdin/stdout, applies the redirections, runs the builtin, then restores. Without this, `pwd > out.txt` would leak the redirected stdout into subsequent commands.

### `mish.c`

The biggest file. Five things live here:

#### 1. The REPL

`getline()` is more careful than `fgets()` — it grows the buffer as needed, so very long lines work. We strdup the line into a `work` buffer because the tokenizer mutates it; if a future feature wanted history we'd want the original line preserved.

EOF on stdin (Ctrl-D at the prompt) breaks out cleanly with a newline. EINTR (signal interrupted the read) is retried — without this, `Ctrl-C` at the prompt would close the shell.

#### 2. Signal handling

```
SIGINT, SIGQUIT, SIGTSTP — ignored at the shell level
SIGCHLD                  — reap with WNOHANG in a loop
```

The SIGCHLD reaper is needed because backgrounded children would otherwise become zombies. The loop matters: multiple children can exit "simultaneously" but Linux only delivers one SIGCHLD per delivery (signals are not queued); the handler must drain everything available with `waitpid(-1, NULL, WNOHANG)` until it returns 0 or -1.

`SA_RESTART` on the SIGCHLD handler asks the kernel to restart interrupted syscalls automatically. Without it, `read()` and friends in the parent would return with `EINTR` every time a child exits, which is annoying.

After fork, every child re-installs `SIG_DFL` for SIGINT/SIGQUIT/SIGTSTP. Without that, the inherited "ignore" disposition would mean Ctrl-C wouldn't kill `sleep 100` — it would be silently ignored.

#### 3. `apply_redirections()`

Opens any `<` / `>` / `>>` files and `dup2`'s them onto stdin/stdout. Called inside the child after fork (or, for non-piped builtins, in the shell with fd save/restore around it).

`O_APPEND` versus `O_TRUNC` is the only difference between `>>` and `>`. Both use `O_CREAT` with mode 0666 (the user's umask masks this down, typically to 0644).

#### 4. `run_simple()` — single command, no pipe

Splits two ways:
- **Builtin** → runs in the shell (with fd save/restore if there are redirections).
- **External** → fork, `apply_redirections()` in the child, `execvp()`, on failure `_exit(127)`. Parent waits if `!bg`.

`_exit` not `exit` in the child after exec failure — `exit()` would flush stdio buffers, which might double-print things the parent already printed.

#### 5. `run_pipeline()` — N-stage pipeline

This is the one piece of cleverness. Given `cmd1 | cmd2 | cmd3`, we need:

- 2 pipes (n-1 for n commands)
- 3 forked children
- Each child has stdin from the previous pipe and stdout to the next
- The parent must close every pipe fd, otherwise the readers never see EOF

The wiring loop for child `i`:

```
if i > 0:    dup2(pipes[i-1][0], STDIN_FILENO)   // read from previous
if i < n-1:  dup2(pipes[i][1],   STDOUT_FILENO)  // write to next

for j in 0..n-1:                                  // close ALL pipe fds
    close(pipes[j][0])
    close(pipes[j][1])
```

Closing all pipe fds in the child after dup2 is non-negotiable. If we leave `pipes[0][0]` open in cmd2's process, then when cmd1 exits, cmd2 still has a reader on its own pipe, and... actually that's the wrong direction. The real bug: if we leave `pipes[0][1]` open in cmd2, then cmd2 still holds a writer to its own input pipe, so when cmd1 exits, the read end never gets EOF, and cmd2 blocks forever in `read()`. Same in the parent — the parent must close both ends of every pipe so the children's reads can EOF.

Per-command file redirections are applied *after* pipe wiring with `apply_redirections()`. This means `ls | grep token > out.txt` works correctly: the pipe wiring sets stdout to the pipe-write-end, then `> out.txt` overrides it back to the file. Standard shell semantics.

For background pipelines, the parent doesn't `waitpid` — the SIGCHLD handler reaps as the children finish. For foreground pipelines, the parent waits for every child in order. The exit status of the pipeline is the last command's status (Bash compatible — though we don't expose `$?` here).

#### 6. `execute_all()` — group commands into pipelines

Walks the Command array. Extends the current pipeline group while `commands[j].sep == "|"`, then runs the group as a unit. The group's terminating separator (the `;` or `&` after the last `|`) determines `bg`.

Example: `a | b ; c | d &` becomes two groups:
- Group 1: `a | b` (terminated by `;`, bg=0)
- Group 2: `c | d` (terminated by `&`, bg=1)

---

## How to add a new built-in

1. Add a `static int do_foo(Command *cmd)` function to `builtin.c`.
2. Add an entry to the `BUILTINS[]` table at the bottom — `{ "foo", do_foo, "what foo does" }`.

Done. `help` will list it automatically. The executor's `builtin_is()` will recognise it.

If `foo` needs to mutate shell state beyond cwd/prompt/exit-flag, declare a global in `mish.c` and `extern` it from `builtin.c` (the `g_prompt` pattern).

---

## How to add a new shell metacharacter

Harder than adding a builtin. Touches three files:

1. **token.c** — Add the character to `META_CHARS`/`META_STR` (or, for multi-char operators like `>>`, add a special case in the meta-emit logic).
2. **command.h** — Add a `SEP_FOO` constant or a new `Command` field.
3. **command.c** — Update `parse_commands()` to recognise the new separator and fill the new field.
4. **mish.c** — Update `execute_all()` and possibly `run_pipeline()` to react to the new field.

For `&&` and `||`, you'd also need to expose the exit status of each command, which currently we throw away.

---

## Common pitfalls

| Mistake | Symptom | Fix |
|---|---|---|
| Missing `#define _POSIX_C_SOURCE 200809L` | `strdup`, `getline`, `kill`, `sigaction` undeclared with `-std=c11` | Define before any `#include` |
| Forgetting to close pipe fds in parent | Children hang forever in `read()` | Close BOTH ends of EVERY pipe in the parent after fork |
| Using `exit()` not `_exit()` after exec failure | Stdio buffers flushed twice → duplicate output | `_exit(127)` |
| Forgetting `SIG_DFL` reset in child | `Ctrl-C` doesn't kill `sleep 100` started from mish | Re-install defaults right after fork |
| Single-char `WNOHANG` `waitpid` (not in a loop) | Multiple simultaneous exits leak zombies | `while (waitpid(...) > 0)` loop |
| `execvp` with `cmd->argv` that has no terminating NULL | Random crashes | Always end argv with NULL — `build_argv` does this |
| Built-in inside pipeline runs in the shell | Side effects "leak" into wrong process | The executor must fork built-ins inside pipelines |
| `O_TRUNC` for `>>` | `>>` overwrites instead of appending | Use `O_APPEND` for `append_stdout==1` |

---

## Why this layout?

Could be smaller. The original Week 3 `Unix_Shell` does roughly the same thing in 519 lines. `mish` is a bit longer because:

- The tokenizer handles `>>` and adjacent metachars cleanly.
- The parser owns argv memory and frees it on error.
- The executor supports arbitrary-length pipelines.
- Built-in redirections are handled (without leaking fds back into the shell).
- Errors are routed to stderr with command-name prefixes.

The cost is more code per feature. The payoff is fewer subtle bugs and a cleaner extension path. Adding a new builtin is two lines; adding a metacharacter is contained to three files with clear contracts between them.
