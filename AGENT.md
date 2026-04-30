# AGENT.md — Pico Shell Implementation Guide

This document explains every file in the project, how they connect, and why each design decision was made.

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────┐
│                      psh.c                          │
│  main() → registers specs → read/eval loop          │
│  eval() → registry_find() → spec->run()  ← built-in│
│         → fork + execve               ← external   │
│  signal handlers: SIGCHLD, SIGINT, SIGTSTP          │
│  job list: addjob, deletejob, fgpid, ...            │
└────────────┬──────────────────────────────┬─────────┘
             │ registry_register()          │ extern fns
             ▼                              ▼
┌────────────────────┐      ┌──────────────────────────┐
│   registry.c       │      │  listjobs_external()     │
│  array of          │      │  do_bgfg_external()      │
│  cmd_spec_t ptrs   │      │  (bridge to job list)    │
└────────────────────┘      └──────────────────────────┘
             ▲
             │ registry_register(&spec)
    ┌────────┴──────────────────────────────┐
    │  cmd_quit.c   cmd_jobs.c              │
    │  cmd_bgfg.c   cmd_help.c              │
    │                                       │
    │  Each file:                           │
    │  - build_XXX_argtable()  ← one defs  │
    │  - XXX_run()             ← uses it   │
    │  - XXX_print_usage()     ← uses it   │
    │  - cmd_XXX_spec          ← exports   │
    └───────────────────────────────────────┘
```

---

## File-by-File Walkthrough

### `cmd_spec.h` — the standard interface

Defines the struct that every command module must implement:

```c
typedef struct cmd_spec {
    const char *name;
    const char *summary;
    const char *long_help;
    int  (*run)(int argc, char **argv);
    void (*print_usage)(FILE *out);
} cmd_spec_t;
```

Every command provides exactly one `cmd_spec_t`. This makes all commands interchangeable, self-documenting, and registerable without touching the shell core.

Also declares:
- `registry_register()` — add a spec
- `registry_find()` — look up by name
- `registry_list()` — print all commands (used by `help`)

---

### `registry.c` — in-memory command registry

A simple array of `cmd_spec_t` pointers with three functions:

```c
int  registry_register(const cmd_spec_t *spec);
const cmd_spec_t *registry_find(const char *name);
void registry_list(FILE *out);
```

At shell startup, `main()` calls `registry_register()` for every built-in.
`eval()` calls `registry_find()` before deciding whether to fork.
`cmd_help.c` calls `registry_list()` and `spec->print_usage()`.

---

### `psh.c` — shell core

Three key parts:

**1. `eval()` dispatches through the registry:**
```c
const cmd_spec_t *spec = registry_find(argv[0]);
if (spec != NULL) {
    spec->run(argc, argv);  // no fork for built-ins
    return;
}
// fork + execve for external programs
```

**2. `main()` registers all specs at startup:**
```c
registry_register(&cmd_quit_spec);
registry_register(&cmd_jobs_spec);
registry_register(&cmd_bg_spec);
registry_register(&cmd_fg_spec);
registry_register(&cmd_help_spec);
```

**3. Two bridge functions expose the job list to command modules:**
```c
void listjobs_external(FILE *out);   // called by cmd_jobs.c
void do_bgfg_external(char **argv);  // called by cmd_bgfg.c
```

Everything else — signal handlers, job list helpers, parseline, waitfg — is standard Unix shell implementation.

---

### `cmd_quit.c` — simplest command example

Shows the full pattern with minimal complexity:

```c
// Step 1: one argtable builder — single source of truth for options
static void build_quit_argtable(
    struct arg_lit **help,
    struct arg_end **end,
    void ***argtable_out) { ... }

// Step 2: print_usage calls the builder
void quit_print_usage(FILE *out) { ... }

// Step 3: run calls the builder, parses, runs logic
int quit_run(int argc, char **argv) { ... }

// Step 4: the spec descriptor
const cmd_spec_t cmd_quit_spec = {
    .name        = "quit",
    .summary     = "exit the shell",
    .long_help   = "Terminate the psh shell session immediately.",
    .run         = quit_run,
    .print_usage = quit_print_usage,
};
```

Copy this pattern for every new command you add.

---

### `cmd_jobs.c` — command with external dependency

Same pattern as `cmd_quit.c`, but `jobs_run()` needs the job list.
Since the job list lives in `psh.c`, a bridge function is used:

```c
extern void listjobs_external(FILE *out);

int jobs_run(int argc, char **argv) {
    // parse args ...
    listjobs_external(stdout);
    return 0;
}
```

This keeps the job list private to `psh.c` while letting command modules use it.

---

### `cmd_bgfg.c` — two commands sharing one argtable

`bg` and `fg` take the same arguments so they share one argtable builder.
The logic differs only in whether the job becomes BG or FG:

```c
static int bgfg_run(const char *cmd, int argc, char **argv) {
    // parse with shared argtable
    // call do_bgfg_external()
}

int bg_run(int argc, char **argv) { return bgfg_run("bg", argc, argv); }
int fg_run(int argc, char **argv) { return bgfg_run("fg", argc, argv); }
```

Two separate `cmd_spec_t` structs are exported but the implementation is shared.

---

### `cmd_help.c` — help system

Two modes:

**No argument** — calls `registry_list()`:
```
psh> help

Available commands:
  quit          exit the shell
  jobs          list background and stopped jobs
  ...
```

**With a command name** — looks it up and calls `print_usage()`:
```
psh> help bg

Usage: bg [-h] %job|pid
...
```

Documentation is auto-derived from the `cmd_spec_t` structs — no separate help strings to maintain.

---

## argtable2 Quick Reference

| Function | What it creates |
|---|---|
| `arg_lit0("h","help","desc")` | optional `-h`/`--help` flag |
| `arg_str0(NULL,NULL,"name","desc")` | optional positional string arg |
| `arg_str1(NULL,NULL,"name","desc")` | required positional string arg |
| `arg_file0(NULL,NULL,"file","desc")` | optional file path argument |
| `arg_end(20)` | error collector — always last |
| `arg_parse(argc,argv,argtable)` | parse — returns error count |
| `arg_print_syntax(out,argtable,"\n")` | print usage line |
| `arg_print_glossary(out,argtable,"  %-20s %s\n")` | print options table |
| `arg_print_errors(out,end,"cmd")` | print parse error messages |

---

## How to Add a New Command

**Step 1** — create `cmd_foo.c`:
```c
#include <argtable2.h>
#include "cmd_spec.h"
extern const cmd_spec_t cmd_foo_spec;

static void build_foo_argtable(...) { /* define options */ }
void foo_print_usage(FILE *out)     { /* use builder */ }
int  foo_run(int argc, char **argv) { /* parse + logic */ }

const cmd_spec_t cmd_foo_spec = {
    .name = "foo", .summary = "...", .long_help = "...",
    .run = foo_run, .print_usage = foo_print_usage,
};
```

**Step 2** — in `psh.c` `main()`, add:
```c
registry_register(&cmd_foo_spec);
```

**Step 3** — in `Makefile`, add `cmd_foo.c` to `PSH_SRCS`.

**Step 4** — run `make`. Done.

---

## Testing Checklist

```bash
# Basic shell tests
echo "./bogus" | ./psh                        # Command not found
echo "./myspin 1" | ./psh                     # Foreground job
printf "./myspin 2 &\njobs\nquit\n" | ./psh   # Background + jobs list
echo "./myint 1"  | ./psh                     # SIGINT termination message
printf "./mystop 1\njobs\nquit\n" | ./psh     # SIGTSTP stop + jobs list

# Help system tests
printf "help\nquit\n" | ./psh                 # List all commands
printf "help jobs\nquit\n" | ./psh            # Per-command help
printf "help bg\nquit\n" | ./psh              # bg help with examples
printf "quit --help\n" | ./psh                # --help flag
printf "bg\nquit\n" | ./psh                   # Error for missing argument
```

---

## Common Mistakes

| Mistake | Fix |
|---|---|
| `print_usage` references spec before it is defined | Add `extern const cmd_spec_t cmd_foo_spec;` at top of file |
| Forgetting `-largtable2` in Makefile | Add `LIBS = -largtable2` and use `$(LIBS)` in link step |
| Calling `waitpid` in `waitfg` | Keep `waitpid` only in `sigchld_handler`; use sleep loop in `waitfg` |
| Not blocking SIGCHLD before fork | Wrap `fork` + `addjob` with `sigprocmask(SIG_BLOCK/UNBLOCK)` |
| Not calling `setpgid(0,0)` in child | ctrl-c kills the shell too — call it before `execve` |
