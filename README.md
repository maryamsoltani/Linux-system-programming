# Pico Shell (psh) — v2 with Command Anatomy

A Unix shell with job control, refactored to follow the **Command Anatomy** pattern from Chapter 2 of the course (`cmd_spec_t` + `argtable3` + command registry).

---

## What Changed from Week 3

| Week 3 (v1) | Week 3 + Chapter 2 (v2) |
|---|---|
| Built-ins handled by one big `builtin_cmd()` | Each built-in is its own module (`cmd_*.c`) |
| `strcmp` for argument parsing | `argtable2/3` — structured parsing with error messages |
| No `--help` support | Every command supports `-h` / `--help` |
| No command listing | `help` command lists all registered commands |
| Hard-coded dispatch in `eval()` | Registry lookup — add commands without touching `eval()` |

---

## Project Structure

```
picoshell/
├── cmd_spec.h      ← The anatomy: cmd_spec_t struct + registry API
├── registry.c      ← In-memory command registry implementation
├── psh.c           ← Shell main loop, eval, signal handlers, job list
├── cmd_quit.c      ← "quit" command module (cmd_spec_t + argtable)
├── cmd_jobs.c      ← "jobs" command module
├── cmd_bgfg.c      ← "bg" and "fg" command modules
├── cmd_help.c      ← "help" command module (uses registry)
├── myspin.c        ← Test helper: sleep N seconds
├── mysplit.c       ← Test helper: fork child that sleeps N seconds
├── mystop.c        ← Test helper: sleep then send SIGTSTP to itself
├── myint.c         ← Test helper: sleep then send SIGINT to itself
├── Makefile        ← Builds everything
├── README.md       ← This file
└── AGENT.md        ← Full implementation walkthrough
```

---

## Command Anatomy — the core idea

Every command in this shell follows a standard shape defined in `cmd_spec.h`:

```c
typedef struct cmd_spec {
    const char *name;         // "jobs"
    const char *summary;      // one-line description for help listings
    const char *long_help;    // full description shown by --help

    int  (*run)(int argc, char **argv);   // argtable3 parses, logic runs
    void (*print_usage)(FILE *out);       // help output — same argtable defs
} cmd_spec_t;
```

Each command module defines **exactly one** `cmd_spec_t` and registers it at startup:

```c
// in main(), before the read/eval loop:
registry_register(&cmd_quit_spec);
registry_register(&cmd_jobs_spec);
registry_register(&cmd_bg_spec);
registry_register(&cmd_fg_spec);
registry_register(&cmd_help_spec);
```

`eval()` then dispatches via the registry — no more hard-coded `if/else` chains:

```c
const cmd_spec_t *spec = registry_find(argv[0]);
if (spec != NULL) {
    spec->run(argc, argv);  // built-in: no fork needed
    return;
}
// else: fork + execve for external programs
```

---

## How to Build

```bash
make
```

Requires `libargtable2-dev` (or `libargtable3`):
```bash
sudo apt-get install libargtable2-dev
```

Clean:
```bash
make clean
```

---

## How to Run

```bash
./psh
```

---

## Built-in Commands

| Command | Description |
|---|---|
| `quit` | Exit the shell |
| `jobs` | List all background and stopped jobs |
| `bg <job>` | Resume a stopped job in the background |
| `fg <job>` | Bring a job to the foreground |
| `help` | List all registered commands |
| `help <cmd>` | Show detailed usage for a specific command |

Every command also accepts `-h` / `--help`:
```
psh> jobs --help
psh> bg --help
psh> help --help
```

---

## Adding a New Command

To add a new command `foo`, you only need to:

1. Create `cmd_foo.c` with:
   - `build_foo_argtable()` — defines options once
   - `foo_run()` — uses argtable to parse, then runs logic
   - `foo_print_usage()` — uses same argtable to print help
   - `cmd_foo_spec` — the `cmd_spec_t` descriptor

2. Add one line to `main()` in `psh.c`:
   ```c
   registry_register(&cmd_foo_spec);
   ```

3. Add `cmd_foo.c` to `PSH_SRCS` in the Makefile.

No changes needed to `eval()`, `builtin_cmd()`, or anything else.

---

## Signal Handling (unchanged from Week 3)

| Key | Signal | Effect |
|---|---|---|
| ctrl-c | SIGINT | Terminates the current foreground job |
| ctrl-z | SIGTSTP | Stops the current foreground job |

Signals are forwarded to the foreground job's **process group** (`kill(-pid, sig)`), so child processes of the job are also affected.

---

## Example Session

```
psh> help

Available commands:

  COMMAND       DESCRIPTION
  -------       -----------
  quit          exit the shell
  jobs          list background and stopped jobs
  bg            resume a stopped job in the background
  fg            bring a job to the foreground
  help          show help for built-in commands

Type '<command> --help' for detailed usage.

psh> ./myspin 3 &
[1] (1234) ./myspin 3 &
psh> jobs
[1] (1234) Running  ./myspin 3 &
psh> fg %1
psh> quit
```

---

## Limitations

- No pipes (`|`) or I/O redirection (`<`, `>`)
- Maximum 16 concurrent jobs
- Do not run interactive programs (`vi`, `emacs`, `less`) from this shell
