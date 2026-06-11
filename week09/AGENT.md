# AGENT.md — Week 09: Sockets Server + FTP + MCP

This file is written for an AI assistant working on this project.
Read it before writing, refactoring, or extending any code here.

---

## What this project is

Week 09 builds the server side of socket programming.

1. **`ftpd/ftpd.c`** — A pedagogical FTP server that works with the real Linux
   `ftp` client. Implements the RFC-style FTP command/response protocol over two
   TCP connections (control + data). Concurrency via detached `pthreads`.

2. **`agent/week09_mcp_agent.ipynb`** — A notebook demonstrating how an
   MCP-like C server (tools as JSON over TCP) can be driven by an AI agent.
   Builds on the Week 08 MCP concept; adds a mocked agent loop and OpenAI notes.

---

## Repository layout

```
week09/
├── AGENT.md              this file
├── README.md             project overview, build steps, test procedure, work log
├── ftpd/
│   ├── ftpd.c            FTP server: pthreads, PASV+PORT, all required commands
│   ├── ftpd.conf         runtime configuration (port, feature flags, limits)
│   └── Makefile          builds `ftpd`; zero warnings required
└── agent/
    └── week09_mcp_agent.ipynb  MCP-like C server + Python client + mocked agent
```

---

## Part 1 — FTP server (`ftpd/ftpd.c`)

### Key types

```c
typedef struct {
    int  ctrl_fd;          // control socket for this session
    char cwd[PATH_MAX];    // current working directory (always within home)
    char home[PATH_MAX];   // FTP root — set to $HOME on connect
    char username[64];
    int  logged_in;
    /* active (PORT) mode */
    int  port_set;
    char data_host[64];
    int  data_port;
    /* passive (PASV) mode */
    int  pasv_fd;          // -1 if no PASV listen socket open
} session_t;
```

One `session_t` is allocated per accepted connection, passed to the thread,
and freed inside the thread before it exits.

### Thread lifecycle

```
main():
    accept()
    calloc(session_t)
    pthread_create(DETACHED, handle_client, session)
    ← immediately loops back to accept()

handle_client():
    SO_RCVTIMEO on ctrl_fd
    send 220 greeting
    loop: read bytes → accumulate line buffer → dispatch command
    QUIT / disconnect: close pasv_fd if open, close ctrl_fd, free session, return
```

Threads are `PTHREAD_CREATE_DETACHED` so the main thread never joins them.
Each thread owns its `session_t` exclusively — no shared state, no locks needed.

### Data connection helpers

`open_data_conn(session_t *s)` — opens a data socket and returns its fd:
- If `s->pasv_fd >= 0`: calls `accept()` on the PASV listen socket,
  closes `pasv_fd`, returns the accepted fd.
- If `s->port_set`: calls `connect()` to the client's host:port (PORT mode).

Always close the returned fd after the transfer is complete.
Always reset `s->port_set = 0` (or `s->pasv_fd = -1`) after use.

### Path safety

`resolve_path(session_t *s, ftp_path, out, len)`:
- FTP `/` → `$HOME`
- FTP `/foo` → `$HOME/foo`
- relative → `$cwd/foo`
- Calls `realpath()` to canonicalise, then checks the result starts with
  `s->home`. Returns `-1` if it escapes home — caller must send 550.

**Never skip the return-value check on `resolve_path`.** A `-1` means the
request is out-of-bounds; send `550 Permission denied` and return.

### Command response codes

| Situation | Code |
|-----------|------|
| Greeting | 220 |
| Password required | 331 |
| Logged in | 230 |
| Command OK | 200 |
| PASV/PORT OK | 200 / 227 |
| Opening data | 150 |
| Transfer complete | 226 |
| Directory listing OK | 226 |
| Directory created | 257 |
| CWD OK | 250 |
| PWD | 257 |
| QUIT | 221 |
| File not found | 550 |
| Dir exists | 550 |
| No data connection | 425 |
| Can't open file | 452 |
| File size exceeded | 552 |
| Bad syntax | 501 |
| Unknown command | 500 |

### Binary transfer rules

STOR and RETR use `read(fd, buf, BUF_SIZE)` / `write(fd, buf, n)` loops — never
`fgets`, `fputs`, or string functions. This correctly handles files containing
NUL bytes, which is a graded test case.

### Configuration file (`ftpd.conf`)

Loaded once at startup with `load_config(path)`. Keys:

| Key | Type | Effect |
|-----|------|--------|
| `port` | int | Control port number |
| `allow_list` | 0/1 | Enables/disables LIST command |
| `allow_retr` | 0/1 | Enables/disables RETR command |
| `allow_stor` | 0/1 | Enables/disables STOR command |
| `allow_mkd`  | 0/1 | Enables/disables MKD command |
| `max_file_kb` | int | STOR size limit (0 = unlimited) |

No live reload — restart `ftpd` after editing the config.

---

## Part 2 — MCP Agent Notebook (`agent/week09_mcp_agent.ipynb`)

The notebook:
1. Writes a complete C MCP-like server (`/mnt/data/server.c`) that exposes
   `list_files`, `get_time`, and `delete_older_than_days` via newline-delimited
   JSON on port 9000.
2. Writes a Python client (`/mnt/data/client.py`).
3. Contains a mocked agent `agent_flow(user_request)` that calls `list_tools`
   then `call_tool` based on a keyword heuristic.
4. Shows OpenAI API / CLI integration patterns for real LLM use.

The C server in the notebook uses `fork()` per client (not pthreads) to keep
the implementation minimal for teaching. The Week 08 `mcp_server` binary is a
drop-in replacement if the MCP server is already running.

---

## Guidelines for AI assistance on this project

### Adding a new FTP command

1. Add a `cmd_XXX(session_t *s, const char *arg)` function.
2. Add an `else if (!strcmp(cmd_upper, "XXX"))` branch in the dispatch table
   inside `handle_client()`.
3. Use `ctrl_send(s, "NNN message\r\n")` for all responses — never `printf` or
   `write` directly with user-controlled strings.
4. If the command uses a data connection, call `open_data_conn(s)`, check for
   `-1`, do the transfer, then `close(data_fd)` and reset `s->port_set = 0`.

### Adding a config option

1. Add the field to `config_t`.
2. Set a default in the `g_cfg` initialiser.
3. Add a `else if (!strcmp(key, "my_key")) g_cfg.my_field = atoi(val);` branch
   in `load_config()`.
4. Use the field to gate the relevant command handler.

### Protocol framing

FTP uses CRLF (`\r\n`) line endings on the control connection. All `ctrl_send`
calls already include `\r\n`. Never send `\n` alone — some ftp clients will
reject it.

### Thread safety

There is no shared mutable state except `g_cfg`, which is read-only after
startup. No locks are needed. Do not introduce shared mutable state without
adding a `pthread_mutex_t`.

### What not to do

- Do not `exit()` inside `handle_client()` — this kills the whole process, not
  just the thread. Return or jump to `cleanup` instead.
- Do not use `system()` — pass `popen()` with a fixed format string and a
  validated path argument (only for LIST, which already does this).
- Do not call `pthread_join()` on the detached threads — this will return an
  error (EINVAL). They clean up themselves.
- Do not open a PASV listen socket on `INADDR_ANY` for production — bind to
  `INADDR_LOOPBACK` as we do here.
- Do not read client-provided filenames into command strings without validation.
  Always call `resolve_path()` first and reject `-1`.

---

## Quick reference

| Task | Files to change |
|------|-----------------|
| Add a new FTP command | `ftpd.c` — add handler + dispatch entry |
| Change control port | `ftpd.conf` — `port = <n>` |
| Disable a feature | `ftpd.conf` — `allow_xxx = 0` |
| Cap upload size | `ftpd.conf` — `max_file_kb = <n>` |
| Change idle timeout | `ftpd.c` — `CTRL_TIMEOUT_SEC` constant |
| Add a config key | `ftpd.c` — `config_t` + `load_config()` |
| Add MCP tool to notebook | `agent/week09_mcp_agent.ipynb` — server.c cell |
