# AGENT.md — AiShell Week 04: Command Anatomy & Package Management

This file is written for an AI assistant working on this project.
Read it before writing, refactoring, or extending any code here.

---

## What this project is

AiShell is a modular CLI toolkit in C. Every command (ls, cat, pkg, …) is an
independent module that plugs into a central dispatcher via a standard interface
called `cmd_spec_t`. The Week 04 extension adds a `pkg` command that treats
each module as a "package" and can generate metadata and documentation from it
automatically.

The key principle from the course notebook (PackageManagement.ipynb):

> "Once a command is written in the standard pattern, we can automatically
>  derive package metadata and docs from it. The cmd_spec_t is the single
>  source of truth."

---

## Repository layout

```
week04/
├── main.c              dispatcher — routes argv to the right cmd_spec_t
├── Makefile
├── argtable3/          vendored argument-parsing library (do not modify)
├── src/
│   ├── cmd_spec.h      THE central header — cmd_spec_t type + COMMAND_REGISTRY
│   ├── cmd_ls.c        filesystem commands
│   ├── cmd_cat.c
│   ├── cmd_stat.c
│   ├── cmd_headtail.c
│   ├── cmd_fileops.c   cp, mv, rm, mkdir, rmdir, touch
│   ├── cmd_shell.c     pwd, cd, env, export, unset, type
│   ├── cmd_echo.c      echo command
│   ├── cmd_hello.c     hello command
│   ├── cmd_rg.c        regex search
│   ├── cmd_edit.c      structured text editing
│   └── cmd_pkg.c       full package manager
└── packages/           output of `pkg build` — do not edit by hand
```

> **Important:** After editing `src/cmd_spec.h`, always run `make clean && make`.
> The Makefile has no header dependency tracking, so stale `.o` files will
> cause commands to silently disappear from the registry.

---

## The command anatomy — cmd_spec_t

Every command module must define **exactly one** `cmd_spec_t`:

```c
typedef struct cmd_spec {
    const char *name;        // CLI name: "ls", "pkg", ...
    const char *version;     // semver string e.g. "1.0.0" (NULL → defaults to "1.0.0")
    const char *summary;     // one-line description — shown in --list and pkg.json
    const char *long_help;   // longer description (may be NULL)
    const char *category;    // group label: "filesystem", "shell", "search",
                             //              "edit", "package"
    int  (*run)(int argc, char **argv);   // entry point
    void (*print_usage)(FILE *out);       // help text printer
} cmd_spec_t;
```

### Required parts for every command module

1. `<name>_run(int argc, char **argv)` — parses args with argtable3, runs logic
2. `<name>_print_usage(FILE *out)` — prints help using the same argtable3 defs
3. `cmd_spec_t spec_<name>` — the descriptor, always at the bottom of the file

### Minimal example — `hello` command

```c
#include <stdio.h>
#include <stdlib.h>
#include "../argtable3/argtable3.h"
#include "cmd_spec.h"

extern cmd_spec_t spec_hello;

void hello_print_usage(FILE *out) {
    struct arg_lit *opt_h  = arg_lit0("h", "help", "display this help and exit");
    struct arg_str *opt_name = arg_str0("n", "name", "NAME", "name to greet");
    struct arg_end *end    = arg_end(10);
    void *argtable[] = { opt_h, opt_name, end };

    fprintf(out, "Usage: hello");
    arg_print_syntax(out, argtable, "\n");
    fprintf(out, "\n%s\n\nOptions:\n", spec_hello.long_help);
    arg_print_glossary(out, argtable, "  %-22s %s\n");
    arg_free(argtable);
}

int hello_run(int argc, char **argv) {
    struct arg_lit *opt_h    = arg_lit0("h", "help", "display this help and exit");
    struct arg_str *opt_name = arg_str0("n", "name", "NAME", "name to greet");
    struct arg_end *end      = arg_end(10);
    void *argtable[] = { opt_h, opt_name, end };

    int nerrors = arg_parse(argc, argv, argtable);

    if (opt_h->count > 0) { hello_print_usage(stdout); arg_free(argtable); return 0; }
    if (nerrors > 0) {
        arg_print_errors(stderr, end, "hello");
        fprintf(stderr, "Try 'hello --help' for more information.\n");
        arg_free(argtable); return 1;
    }

    const char *name = opt_name->count > 0 ? opt_name->sval[0] : "world";
    printf("Hello, %s!\n", name);
    arg_free(argtable);
    return 0;
}

cmd_spec_t spec_hello = {
    .name        = "hello",
    .summary     = "print a friendly greeting",
    .long_help   = "Print a greeting, optionally addressing a specific NAME.",
    .category    = "example",
    .run         = hello_run,
    .print_usage = hello_print_usage,
};
```

---

## argtable3 — rules and patterns

The project uses **argtable3** (vendored in `argtable3/`). Include it as:

```c
#include "../argtable3/argtable3.h"   // from src/ files
```

### The required pattern

Both `run()` and `print_usage()` must declare and build the argtable
**independently** — do not share a single static instance between them.
Always call `arg_free(argtable)` before every return path.

```c
// Declare argtable variables
struct arg_lit *opt_h = arg_lit0("h", "help", "display this help and exit");
struct arg_str *opt_x = arg_str0("x", "foo",  "VAL", "description");
struct arg_end *end   = arg_end(10);
void *argtable[] = { opt_h, opt_x, end };

// Parse
int nerrors = arg_parse(argc, argv, argtable);

// Check help first, then errors
if (opt_h->count > 0) { print_usage(stdout); arg_free(argtable); return 0; }
if (nerrors > 0) {
    arg_print_errors(stderr, end, "cmdname");
    arg_free(argtable); return 1;
}

// Use values
// opt_x->sval[0]  — string value (only valid when opt_x->count > 0)
// opt_h->count    — number of times flag appeared

arg_free(argtable);
return 0;
```

### Common argtable3 constructors

| Constructor | Meaning |
|---|---|
| `arg_lit0("h","help","desc")` | Optional flag (`-h` / `--help`) |
| `arg_lit1("v","verbose","desc")` | Required flag |
| `arg_str0("n","name","META","desc")` | Optional string option |
| `arg_str1("n","name","META","desc")` | Required string option |
| `arg_strn(NULL,NULL,"FILE",1,10,"desc")` | 1–10 positional string args |
| `arg_int0("n",NULL,"N","desc")` | Optional integer option |
| `arg_file0(NULL,NULL,"PATH","desc")` | Optional file path |
| `arg_end(10)` | Error collector (always last) |

### Printing help in print_usage()

```c
fprintf(out, "Usage: cmdname");
arg_print_syntax(out, argtable, "\n");           // prints option syntax line
fprintf(out, "\n%s\n\nOptions:\n", spec_foo.long_help);
arg_print_glossary(out, argtable, "  %-22s %s\n"); // prints option table
arg_free(argtable);
```

---

## The command registry

`src/cmd_spec.h` holds a static NULL-terminated array of pointers to all
registered `cmd_spec_t` values:

```c
static cmd_spec_t * const COMMAND_REGISTRY[] = {
    &spec_ls, &spec_cat, ..., &spec_pkg,
    NULL
};
```

To iterate over all commands (as `pkg` does):

```c
for (cmd_spec_t * const *s = COMMAND_REGISTRY; *s; s++) {
    printf("%s — %s\n", (*s)->name, (*s)->summary);
}
```

### Adding a new command — checklist

1. Create `src/cmd_<name>.c` following the anatomy above.
2. In `src/cmd_spec.h`:
   - Add `extern cmd_spec_t spec_<name>;` in the correct category block.
   - Add `&spec_<name>,` to `COMMAND_REGISTRY` in the same category group.
3. In `Makefile`:
   - Add `src/cmd_<name>.c` to `CMD_SRC`.
   - Add `<name>` to the `CMDS` list (for symlink creation).
4. Run `make` — fix any warnings before moving on.

---

## The pkg command

`src/cmd_pkg.c` is the package manager. It demonstrates the self-describing
property of the anatomy: it iterates `COMMAND_REGISTRY` and asks each spec
to describe itself.

### Subcommands

| Subcommand | Description |
|---|---|
| `pkg list` | List all commands with install status |
| `pkg build [name]` | Generate `pkg.json` + `usage.txt` (all or one) |
| `pkg pack [name]` | Bundle built package into `.tar.gz` |
| `pkg install <name>` | Install command to `~/.local/bin` as symlink |
| `pkg install <f.tar.gz>` | Install from packed archive |
| `pkg remove <name>` | Uninstall command |
| `pkg installed` | List installed packages |
| `pkg version <name>` | Show version from `cmd_spec_t` |
| `pkg pip <subcmd> [args]` | Forward to system pip |
| `pkg info <name>` | Show full metadata + usage |

Install tracking is stored in `~/.config/aishell/installed` (one name per line).
Symlinks are created in `~/.local/bin/`.

`pkg.json` format:
```json
{
  "name": "hello",
  "version": "1.0.0",
  "category": "shell",
  "summary": "Print a greeting message",
  "description": "..."
}
```

### How pkg derives documentation

```
cmd_spec_t.version      →  pkg.json "version"
cmd_spec_t.summary      →  pkg.json "summary"
cmd_spec_t.long_help    →  pkg.json "description"
cmd_spec_t.category     →  pkg.json "category"
spec->print_usage(file) →  usage.txt
```

No hand-written package manifests. If a command's help text changes, the
next `pkg build` picks it up automatically.

---

## JSON output convention

Commands that produce structured data support a `--json` flag. This is
intentional — it makes commands usable by AI agents and scripts without
parsing human-readable output.

Pattern:
```c
struct arg_lit *opt_j = arg_lit0(NULL, "json", "output JSON");
...
if (opt_j->count > 0)
    printf("{\"key\":\"%s\"}\n", value);
else
    printf("%s\n", value);
```

When writing a new command that returns data (file info, search results,
environment values, etc.), always add `--json`.

---

## Dispatcher (main.c)

`main()` supports two invocation modes:

```
./aishell <command> [args...]   dispatcher mode
./ls [args...]                  symlink mode — argv[0] is the command name
```

Logic:
1. Strip the directory part from `argv[0]` to get `progname`.
2. If `progname == "aishell"`: treat `argv[1]` as the command name, shift argv.
3. Otherwise: `progname` is already the command name.
4. Linear scan of `COMMAND_REGISTRY` → call `spec->run(argc, argv)`.
5. Unknown command → exit 127.

Do not modify `main.c` when adding commands — only `cmd_spec.h` and `Makefile`
need updating.

---

## Guidelines for AI assistance on this project

These are taken directly from the course notebook (PackageManagement.ipynb):

### Set context clearly
When starting a new task, state:
- What you are building (a new command module, a refactor, a pkg feature).
- Target environment: Linux, GCC, argtable3, the `cmd_spec_t` anatomy.
- Constraints: no heavy external libraries; keep code portable C99/C11.

### Break tasks into small steps
- First write a skeleton (`cmd_spec_t`, empty `run`, empty `print_usage`).
- Then implement one feature at a time.
- Build and fix warnings after each step.

### When refactoring an existing command into the anatomy
1. Identify the command's options — map each to an argtable3 constructor.
2. Write `print_usage()` first using those argtable3 defs.
3. Write `run()` using the same defs.
4. Fill in `cmd_spec_t` last.
5. Register in `cmd_spec.h` and `Makefile`.
6. Run `make` — zero warnings is the goal.

### When asked to add a `--json` flag
Follow the pattern in `cmd_ls.c` and `cmd_rg.c`:
- Add `arg_lit0(NULL, "json", "output JSON")` to the argtable.
- Branch on `opt_j->count > 0` in the output section.
- JSON keys should be snake_case strings. Escape quotes and newlines.

### When debugging a build error
- Read the full GCC error — file name and line number are in the message.
- The most common mistakes: forgetting `arg_free`, mismatched argtable
  between `run` and `print_usage`, missing `extern` declaration in
  `cmd_spec.h`, missing entry in `COMMAND_REGISTRY`.

### What not to do
- Do not modify `argtable3/` source files.
- Do not add `static` to `cmd_spec_t spec_<name>` — it must be externally linkable.
- Do not use `printf` for errors — use `fprintf(stderr, ...)`.
- Do not skip `arg_free` — argtable3 heap-allocates its nodes.
- Do not add commands to `COMMAND_REGISTRY` before their `.c` file exists.

---

## Quick reference — file to touch for each task

| Task | Files to change |
|---|---|
| Add a new command | `src/cmd_<name>.c`, `src/cmd_spec.h`, `Makefile` |
| Rename a command | `cmd_spec_t.name` field + `COMMAND_REGISTRY` entry |
| Change a command's version | `cmd_spec_t.version` field in its `.c` file |
| Add `--json` to existing command | `src/cmd_<name>.c` only |
| Add a `pkg` subcommand | `src/cmd_pkg.c` only |
| Change category of a command | `cmd_spec_t.category` field |
| Regenerate all package docs | `./aishell pkg build` (no code change) |
| After editing `cmd_spec.h` | `make clean && make` (required) |
