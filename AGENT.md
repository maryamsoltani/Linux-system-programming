# AGENT.md — Pico Shell v2 Implementation Guide

This document explains every file in the project, how they connect, and
why each design decision was made. It covers both the original Week 3
shell (process control + signals) and the Chapter 2 refactor
(cmd_spec_t + argtable + registry).

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

### `cmd_spec.h` — the anatomy contract

This is the most important file. It defines:

```c
typedef struct cmd_spec {
    const char *name;
    const char *summary;
    const char *long_help;
    int  (*run)(int argc, char **argv);
    void (*print_usage)(FILE *out);
} cmd_spec_t;
```

Every command in the shell must provide exactly one `cmd_spec_t`.
This is what the notebook calls the "anatomy" — a standard shape that
makes all commands interchangeable, self-documenting, and registerable.

Also defines:
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

The main shell file. Compared to Week 3, three things changed:

**1. `eval()` now uses the registry:**
```c
const cmd_spec_t *spec = registry_find(argv[0]);
if (spec != NULL) {
    spec->run(argc, argv);  // no fork for built-ins
    return;
}
// fork + execve for external programs (unchanged)
```

**2. `main()` registers all specs:**
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
This keeps the job list private to `psh.c` while letting the command
modules use it without needing to know the struct layout.

Everything else (signal handlers, job list helpers, parseline, waitfg)
is identical to Week 3.

---

### `cmd_quit.c` — the simplest anatomy example

Shows the full pattern with minimal complexity:

```c
// Step 1: one argtable builder — SINGLE SOURCE OF TRUTH
static void build_quit_argtable(
    struct arg_lit **help,
    struct arg_end **end,
    void ***argtable_out)
{
    *help = arg_lit0("h", "help", "show this help message and exit");
    *end  = arg_end(10);
    // ... assemble array ...
}

// Step 2: print_usage calls the builder
void quit_print_usage(FILE *out) {
    // calls build_quit_argtable, then arg_print_syntax + arg_print_glossary
}

// Step 3: run calls the builder, parses, then runs logic
int quit_run(int argc, char **argv) {
    // calls build_quit_argtable, then arg_parse
    // handles --help, handles errors, then: exit(0)
}

// Step 4: the spec descriptor
const cmd_spec_t cmd_quit_spec = {
    .name        = "quit",
    .summary     = "exit the shell",
    .long_help   = "...",
    .run         = quit_run,
    .print_usage = quit_print_usage,
};
```

This is the template for every command. Copy it and change the logic.

---

### `cmd_jobs.c` — anatomy with an external dependency

Same pattern as `cmd_quit.c`, but `jobs_run()` needs the job list.
Since the job list lives in `psh.c`, we use a bridge function:

```c
extern void listjobs_external(FILE *out);  // defined in psh.c

int jobs_run(int argc, char **argv) {
    // ... parse args ...
    listjobs_external(stdout);  // delegates to psh.c
    return 0;
}
```

This is the clean way to share state between the shell core and
command modules without exposing the entire `jobs[]` array.

---

### `cmd_bgfg.c` — two commands sharing one argtable

`bg` and `fg` take exactly the same arguments, so they share
one `build_bgfg_argtable()` builder. The logic differs only in
whether the job gets state BG or FG. One shared function handles both:

```c
static int bgfg_run(const char *cmd, int argc, char **argv) {
    // parse with shared argtable
    // call do_bgfg_external() with reconstructed argv
}

int bg_run(int argc, char **argv) { return bgfg_run("bg", argc, argv); }
int fg_run(int argc, char **argv) { return bgfg_run("fg", argc, argv); }
```

Two separate `cmd_spec_t` are exported (`cmd_bg_spec`, `cmd_fg_spec`),
but the implementation is shared. This is a good pattern whenever two
commands are very similar.

---

### `cmd_help.c` — the anatomy in action

This command proves the whole system works. It has two modes:

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

The documentation comes entirely from the `cmd_spec_t` structs —
no separate man pages or help strings to maintain.

---

## argtable2/3 Quick Reference

| Function | What it creates |
|---|---|
| `arg_lit0("h","help","desc")` | optional `-h`/`--help` flag |
| `arg_lit1("v","verbose","desc")` | required `-v`/`--verbose` flag |
| `arg_str0(NULL,NULL,"name","desc")` | optional positional string arg |
| `arg_str1(NULL,NULL,"name","desc")` | required positional string arg |
| `arg_file0(NULL,NULL,"file","desc")` | optional file path argument |
| `arg_end(20)` | error collector (always last) |
| `arg_parse(argc,argv,argtable)` | parse — returns error count |
| `arg_print_syntax(out,argtable,"\n")` | print `Usage: cmd [-h] ...` line |
| `arg_print_glossary(out,argtable,"  %-20s %s\n")` | print options table |
| `arg_print_errors(out,end,"cmd")` | print parse error messages |

---

## How to Add a New Command

1. Create `cmd_foo.c`:
```c
#include <argtable2.h>
#include "cmd_spec.h"
extern const cmd_spec_t cmd_foo_spec;  /* forward decl */

static void build_foo_argtable(...) { /* define your options */ }

void foo_print_usage(FILE *out) {
    /* call build_foo_argtable, then arg_print_syntax + arg_print_glossary */
}

int foo_run(int argc, char **argv) {
    /* call build_foo_argtable, arg_parse, handle --help + errors, run logic */
}

const cmd_spec_t cmd_foo_spec = {
    .name = "foo", .summary = "...", .long_help = "...",
    .run = foo_run, .print_usage = foo_print_usage,
};
```

2. In `psh.c` `main()`, add:
```c
registry_register(&cmd_foo_spec);
```

3. In `Makefile`, add `cmd_foo.c` to `PSH_SRCS`.

4. Run `make`. Done.

---

## Testing Checklist

```bash
# All original Week 3 tests
echo "./bogus" | ./psh               # Command not found
echo "./myspin 1" | ./psh            # Foreground job
printf "./myspin 2 &\njobs\nquit\n" | ./psh   # Background + jobs list
echo "./myint 1"  | ./psh            # SIGINT termination message
printf "./mystop 1\njobs\nquit\n" | ./psh     # SIGTSTP stop message

# New Chapter 2 tests
printf "help\nquit\n" | ./psh        # Registry listing
printf "help jobs\nquit\n" | ./psh   # Per-command help via anatomy
printf "help bg\nquit\n" | ./psh     # bg help with examples
printf "quit --help\n" | ./psh       # --help flag on any command
printf "bg\nquit\n" | ./psh          # argtable error message for missing arg
```

---

## Common Mistakes

| Mistake | Fix |
|---|---|
| `print_usage` references `cmd_foo_spec` before it's defined | Add `extern const cmd_spec_t cmd_foo_spec;` at top of file |
| Forgetting `-largtable2` in Makefile | Add `LIBS = -largtable2` and use `$(LIBS)` in link step |
| Calling `waitpid` in `waitfg` | Keep `waitpid` only in `sigchld_handler`; use sleep loop in `waitfg` |
| Not blocking SIGCHLD before fork | Wrap `fork` + `addjob` with `sigprocmask(SIG_BLOCK/UNBLOCK)` |
