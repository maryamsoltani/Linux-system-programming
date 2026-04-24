# AiShell — Agent Guide

AiShell is a single C binary that exposes Unix shell utilities with a `--json` output mode, making it suitable for use by AI agents that need to read and manipulate a filesystem in a structured, parseable way.

---

## Invocation Modes

### Combined binary (dispatcher)
```bash
./aishell <command> [args...]
./aishell --list           # show all registered commands
```

### Symlinked binary (direct)
```bash
ln -sf aishell ls
./ls [args...]
```

Each symlinked command behaves identically to `./aishell <command>`.

---

## Building

```bash
make           # build ./aishell
make symlinks  # create per-command symlinks
make test      # run smoke tests
make clean     # remove build artifacts
```

Requires: `gcc`, standard C library, `libm`.

---

## Command Reference

### Filesystem

| Command | Summary | Key Flags |
|---------|---------|-----------|
| `ls` | List directory contents | `-l` long format, `-a` show hidden, `--json` |
| `stat` | File/directory metadata | `--json` |
| `cat` | Print file contents | `-n` number lines, `--json` |
| `head` | Print first N lines | `-n N`, `--json` |
| `tail` | Print last N lines | `-n N`, `--json` |
| `cp` | Copy file | |
| `mv` | Move/rename file | |
| `rm` | Remove file | |
| `mkdir` | Create directory | |
| `rmdir` | Remove directory | |
| `touch` | Create or update file timestamp | |

### Shell / Environment

| Command | Summary | Key Flags |
|---------|---------|-----------|
| `pwd` | Print working directory | `--json` |
| `cd` | Change directory | |
| `env` | List environment variables | `--json` |
| `export` | Set environment variable | |
| `unset` | Unset environment variable | |
| `type` | Show command type/location | |

### Search

| Command | Summary | Key Flags |
|---------|---------|-----------|
| `rg` | Search text with regex | `-n` line numbers, `-i` case-insensitive, `-w` whole word, `--fixed-strings`, `-C N` context lines, `--json` |

### Structured Editing

| Command | Summary | Usage |
|---------|---------|-------|
| `edit-replace-line` | Replace a line by number | `FILE N TEXT` |
| `edit-insert-line` | Insert a line at position | `FILE N TEXT` |
| `edit-delete-line` | Delete a line by number | `FILE N` |
| `edit-replace` | Find-and-replace text in file | `FILE PATTERN REPLACEMENT` |

---

## JSON Output Mode

All commands that support `--json` output structured data suitable for agent parsing. This is the recommended mode when used programmatically.

### Examples

```bash
# List directory as JSON
./aishell ls --json /tmp
# Output: [{"name":"file.txt","type":"file","size":42,"mtime":"2026-04-23T18:00:00"}, ...]

# Read a file as JSON
./aishell cat --json myfile.c
# Output: {"path":"myfile.c","content":"...escaped content..."}

# Search with JSON output
./aishell rg --json --fixed-strings "TODO" src/
# Output: {"file":"src/main.c","line":12,"text":"// TODO: fix this"}

# Get working directory as JSON
./aishell pwd --json
# Output: {"cwd":"/home/user/project"}

# File metadata as JSON
./aishell stat --json myfile.c
# Output: {"path":"myfile.c","size":1234,"mtime":"...","type":"file", ...}
```

---

## Editing Files

The edit commands modify files in-place and are 1-based (first line = line 1).

```bash
# Replace line 3 with new content
./aishell edit-replace-line file.txt 3 "new content here"

# Insert a line before line 2
./aishell edit-insert-line file.txt 2 "inserted line"

# Delete line 5
./aishell edit-delete-line file.txt 5

# Replace all occurrences of "foo" with "bar"
./aishell edit-replace file.txt "foo" "bar"
```

---

## Agent Usage Pattern

A typical agent workflow using AiShell:

```bash
# 1. Explore the directory
./aishell ls --json .

# 2. Read a file
./aishell cat --json src/main.c

# 3. Search for a pattern
./aishell rg --json "TODO" src/

# 4. Edit a specific line
./aishell edit-replace-line src/main.c 42 "    return 0;"

# 5. Verify the change
./aishell cat --json src/main.c
```

---

## Adding a New Command

1. Create `src/cmd_<name>.c`
2. Define a `cmd_spec_t spec_<name>` with `name`, `summary`, `long_help`, `category`, `run`, and `print_usage`
3. Declare `extern cmd_spec_t spec_<name>;` in `src/cmd_spec.h`
4. Add `&spec_<name>` to `COMMAND_REGISTRY[]` in `src/cmd_spec.h`
5. Add `src/cmd_<name>.c` to `CMD_SRC` in `Makefile`
6. Run `make`

---

## Project Structure

```
aishell/
├── main.c                  # dispatcher — resolves argv[0] or argv[1] to a command
├── Makefile
├── src/
│   ├── cmd_spec.h          # cmd_spec_t definition and COMMAND_REGISTRY
│   ├── cmd_ls.c            # ls
│   ├── cmd_stat.c          # stat
│   ├── cmd_cat.c           # cat
│   ├── cmd_headtail.c      # head, tail
│   ├── cmd_fileops.c       # cp, mv, rm, mkdir, rmdir, touch
│   ├── cmd_shell.c         # pwd, cd, env, export, unset, type
│   ├── cmd_rg.c            # rg
│   └── cmd_edit.c          # edit-replace-line, edit-insert-line, edit-delete-line, edit-replace
└── argtable3/              # vendored argument parsing library
```

---

## Notes

- All commands support `--help` / `-h` for usage information.
- Exit code `0` = success, non-zero = error (following Unix convention).
- Exit code `127` = unknown command (following shell convention).
- The `--json` flag on edit commands outputs a status object confirming the operation.
- `rg` defaults to searching the current directory if no path is given.
- Line numbers in edit commands are **1-based**.
