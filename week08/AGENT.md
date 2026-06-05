# AGENT.md — Week 08: Sockets (Client) + MCP Agent Integration

This file is written for an AI assistant working on this project.
Read it before writing, refactoring, or extending any code here.

---

## What this project is

Week 08 extends the AiShell from Week 06 with two new capabilities:

1. **`net-get`** — a TCP socket client command integrated into the shell.
   Implements the Session 8 client pattern exactly:
   `socket() → connect() → send()/recv() → close()`

2. **`mcp_server`** — a fork-per-client TCP server (port 9000) that exposes
   every registered AiShell command as a JSON tool callable by an AI agent.

3. **`agent/week08_agent.ipynb`** — a Python notebook connecting an OpenAI
   agent to `mcp_server` so natural-language queries route through the C shell.

---

## Repository layout

```
week08/
├── AGENT.md                        this file
├── README.md                       project overview, build steps, work log
├── aishell/
│   ├── AGENT.md                    aishell-specific AI guidance (command patterns)
│   ├── Makefile                    builds `aishell` and `mcp_server`
│   ├── main.c                      interactive REPL (unchanged from week06)
│   ├── cmd_spec.h                  cmd_spec_t — the contract every command fulfills
│   ├── registry.c                  register / find / iterate commands
│   ├── register_all_commands.c     wires all register_*_command() calls
│   ├── json_utils.{c,h}            json_print_string() helper
│   ├── cmd_netget.{c,h}            NEW — net-get socket client command
│   ├── mcp_server.c                NEW — MCP TCP server (port 9000)
│   └── cmd_*.{c,h}                 23 original built-in commands
└── agent/
    └── week08_agent.ipynb          Python OpenAI agent → MCP → C shell
```

---

## Part 1 — `net-get` command (`cmd_netget.c`)

### Purpose
Send an HTTP/1.0 GET request using raw POSIX sockets, print the response body.

### Key design decisions
- **Timeouts**: `SO_RCVTIMEO` and `SO_SNDTIMEO` are set via `setsockopt()` before
  `connect()`. Default is 5 seconds. This prevents the shell from ever hanging.
- **Name resolution**: `getaddrinfo()` with `AF_UNSPEC` supports both IPv4 and IPv6.
- **Output modes**: plain text body (default), `--headers` for HTTP headers only,
  `--json` for a stable machine-readable schema.
- **Exit code**: 0 for HTTP 2xx/3xx; 1 for network error or HTTP 4xx/5xx.

### Flags
```
net-get <host> [--port <n>] [--path <path>] [--timeout <sec>] [--headers] [--json]
```

### Adding it to a new project
1. Add `cmd_netget.c` to `SRC` in `Makefile`.
2. Declare `void register_netget_command(void);` in `register_all_commands.c`.
3. Call `register_netget_command();` inside `register_all_builtin_commands()`.

---

## Part 2 — `mcp_server` (`mcp_server.c`)

### Purpose
A minimal TCP server that exposes AiShell commands as JSON tools so an AI agent
(or any HTTP client) can call them programmatically.

### Protocol
One JSON request per connection, one JSON response, then close.

**Request** (newline-terminated):
```json
{"tool": "ls", "args": ["/tmp"]}
```

**Response**:
```json
{"tool": "ls", "exit_code": 0, "output": "file1\nfile2\n"}
```

**Discovery** (list all tools):
```json
{"tool": "__list__"}
→ {"tools": [{"name": "ls", "summary": "list directory contents"}, ...]}
```

### Architecture: fork-per-client
Each accepted connection is handled in a child process:
```
accept() → fork()
  child:  parse JSON → find_command() → pipe() → fork() → cmd->run() → return JSON
  parent: close(cfd), waitpid(NOCLDWAIT via SIGCHLD)
```
The inner fork captures the command's stdout/stderr via a pipe so output is
returned as a JSON string rather than printed to the terminal.

### Key constants (`mcp_server.c`)
| Constant | Value | Meaning |
|----------|-------|---------|
| `DEFAULT_PORT` | 9000 | TCP port |
| `MAX_REQUEST` | 4096 | Max request bytes |
| `MAX_ARGS` | 64 | Max args per tool call |
| `MAX_OUTPUT` | 65536 | Max captured output bytes |

### JSON parser (`parse_request`)
A minimal hand-written parser — no external library. Understands:
- Top-level `{"tool": "...", "args": ["...", ...]}` only.
- JSON string escapes: `\"`, `\\`, `\n`, `\t`, `\r`.
- Unknown keys are skipped safely.

### `list_ctx` pattern
`for_each_command()` requires a plain function pointer. To avoid nested
functions (which require executable stack trampolines), a `struct list_ctx`
carries the file descriptor and a `first` flag, passed as `void *userdata`.

---

## Part 3 — Python agent (`agent/week08_agent.ipynb`)

### mcp_call(tool, args)
Raw TCP socket: `create_connection → sendall(JSON\n) → recv until \n → parse`.
Timeout is set via `socket.create_connection(..., timeout=TIMEOUT)`.

### mcp_call_logged(tool, args)
Wraps `mcp_call` with `logging.info()` lines — satisfies the assignment
requirement that all agent-to-MCP calls are logged.

### build_openai_tools(tool_list)
Converts the `__list__` response into OpenAI function-calling schemas.
Tool names with `-` are sanitised to `_` (OpenAI name constraint).
`name_map` reverses this for the actual MCP dispatch.

### run_agent(query)
Standard OpenAI tool-use loop:
```
messages = [system_prompt, user_query]
loop:
    call chat.completions.create(tools=openai_tools)
    if no tool_calls → return final answer
    for each tool_call:
        dispatch via mcp_call_logged()
        append tool result to messages
```

### delete_older_than_days(directory, days, dry_run=True)
Uses `mcp_call_logged("ls", [directory])` then `os.path.getmtime()` locally.
`dry_run=True` (default) only lists candidates — never deletes without explicit opt-in.

---

## Guidelines for AI assistance on this project

### Extending the MCP server
- To add a new tool that is **not** an AiShell command, add it as a new
  `if (strcmp(tool, "my_tool") == 0)` branch before the `find_command()` call
  in `handle_client()`.
- Do not add blocking network calls inside `handle_client()` without a timeout.
- The server must remain single-process in the accept loop; complexity lives
  in child processes.

### Adding a new aishell command (recap)
1. `cmd_myname.{h,c}` — implement `myname_run()`, `myname_print_usage()`,
   `cmd_spec_t`, `register_myname_command()`.
2. Declare and call in `register_all_commands.c`.
3. Add to `SRC` **and** `MCP_SRC` in `Makefile` (both targets need it).
4. `make` — zero warnings required.

### Protocol safety
- Never pass user-controlled strings directly into `dprintf(fd, ...)` format
  strings. Use `json_print_string(FILE*, str)` for any string value.
- The MCP server does **not** authenticate clients. Run it only on loopback
  (127.0.0.1) in development. Do not bind to `0.0.0.0` without adding auth.

### What not to do
- Do not `exit()` from inside a command's `run()` — it kills the child that
  captured its output, but the parent will get an empty response instead of an error.
- Do not add long-running or interactive commands to `mcp_server` — the
  server blocks the child until the command exits.
- Do not use `system()` inside MCP server or command modules.
- Do not add OpenAI API keys to source files or notebooks — use environment
  variables (`OPENAI_API_KEY`).

---

## Quick reference

| Task | Files to change |
|------|-----------------|
| Add a new aishell command | `cmd_<name>.{h,c}`, `register_all_commands.c`, `Makefile` (both `SRC` and `MCP_SRC`) |
| Add a new MCP-only tool | `mcp_server.c` — `handle_client()` |
| Change MCP listen port | `mcp_server.c` — `DEFAULT_PORT` |
| Add a new OpenAI tool | `agent/week08_agent.ipynb` — `build_openai_tools()` or `run_agent()` |
| Change socket timeout in net-get | `cmd_netget.c` — `DEFAULT_TIMEOUT` |
| Change MCP output size limit | `mcp_server.c` — `MAX_OUTPUT` |
