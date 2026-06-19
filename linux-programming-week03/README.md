# Pico Shell (psh)

A simple Unix shell with job control, written in C. Supports running programs, managing foreground and background jobs, and responding to keyboard signals like ctrl-c and ctrl-z.

---

## Project Structure

```
picoshell/
├── cmd_spec.h      ← Standard interface shared by all command modules
├── registry.c      ← In-memory command registry
├── psh.c           ← Shell main loop, eval, signal handlers, job list
├── cmd_quit.c      ← "quit" command module
├── cmd_jobs.c      ← "jobs" command module
├── cmd_bgfg.c      ← "bg" and "fg" command modules
├── cmd_help.c      ← "help" command module
├── myspin.c        ← Test helper: sleep N seconds
├── mysplit.c       ← Test helper: fork child that sleeps N seconds
├── mystop.c        ← Test helper: sleep then send SIGTSTP to itself
├── myint.c         ← Test helper: sleep then send SIGINT to itself
├── Makefile        ← Builds everything
├── README.md       ← This file
└── AGENT.md        ← Full implementation walkthrough
```

---

## How to Build

Install the required library first:
```bash
sudo apt-get install libargtable2-dev
```

Then build:
```bash
make
```

Clean compiled files:
```bash
make clean
```

---

## How to Run

```bash
./psh
```

You will see the prompt:
```
psh>
```

Type commands just like a normal shell. Press ctrl-D or type `quit` to exit.

---

## Built-in Commands

| Command | Description |
|---|---|
| `quit` | Exit the shell |
| `jobs` | List all background and stopped jobs |
| `bg <job>` | Resume a stopped job in the background |
| `fg <job>` | Bring a job to the foreground |
| `help` | List all built-in commands |
| `help <cmd>` | Show detailed usage for a specific command |

Every command also accepts `-h` / `--help`:
```
psh> jobs --help
psh> bg --help
psh> quit --help
```

Jobs can be referred to by **JID** (e.g., `%1`) or **PID** (e.g., `9721`).

---

## Signal Handling

| Key | Signal | Effect |
|---|---|---|
| ctrl-c | SIGINT | Terminates the current foreground job |
| ctrl-z | SIGTSTP | Stops the current foreground job |

Signals are forwarded to the foreground job's entire process group so all child processes are also affected.

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
psh> ./myspin 5 &
[2] (1235) ./myspin 5 &
psh> jobs
[1] (1234) Running  ./myspin 3 &
[2] (1235) Running  ./myspin 5 &
psh> fg %1
Job [1] (1234) stopped by signal: Stopped
psh> bg %1
[1] (1234) ./myspin 3 &
psh> quit
```

---

## Design Notes

### Process group isolation
Each child process calls `setpgid(0, 0)` after `fork()` and before `execve()`. This puts the child in its own process group so ctrl-c and ctrl-z only affect the child, not the shell itself.

### Preventing race conditions
`SIGCHLD` is blocked using `sigprocmask` before `fork()`. The parent calls `addjob()` before unblocking. This guarantees the signal handler can never delete a job before it has been added.

### Modular command structure
Each built-in command is its own `.c` file with its own argument parsing and help text. Commands are registered at startup and looked up by name at runtime — no hard-coded if/else chains.

---

## Limitations

- No support for pipes (`|`) or I/O redirection (`<`, `>`)
- Maximum 16 concurrent jobs
- Do not run interactive programs like `vi`, `emacs`, or `less` from this shell
