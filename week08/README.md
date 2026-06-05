# Week 08 — Sockets (Client) + First Remote Integrations

Extends the AiShell from Week 06 with two new capabilities:

1. **`net-get`** — a TCP socket client command built into the shell
2. **`mcp_server`** — a fork-per-client TCP server that exposes every
   registered shell command as a JSON tool callable by an AI agent

---

## Project layout

```
week08/
  aishell/
    cmd_netget.{c,h}        <- socket client command (Session 8 pattern)
    mcp_server.c            <- MCP server: JSON over TCP, fork-per-client
    cmd_*.{c,h}             <- all 23 original AiShell commands
    main.c                  <- interactive shell REPL (unchanged)
    registry.{c,h}          <- command registry (unchanged)
    Makefile                <- builds both `aishell` and `mcp_server`
  agent/
    week08_agent.ipynb      <- Python notebook: OpenAI agent → MCP → C shell
```

---

## Quick start

```bash
cd week08/aishell

# Build everything (aishell + mcp_server)
make

# Run the interactive shell
./aishell

# Test the new net-get command
./aishell net-get example.com
./aishell net-get example.com --headers
./aishell net-get example.com --json

# Start the MCP server (background)
./mcp_server &

# Probe it manually
echo '{"tool":"__list__"}' | nc -q1 127.0.0.1 9000
echo '{"tool":"echo","args":["hello","world"]}' | nc -q1 127.0.0.1 9000
echo '{"tool":"ls","args":["/tmp"]}' | nc -q1 127.0.0.1 9000
```

---

## Part 1 — `net-get` (socket client command)

Implements the Session 8 client pattern exactly:

```
socket() → connect() → send() → recv() → close()
```

Key design decisions:
- `SO_RCVTIMEO` / `SO_SNDTIMEO` enforce a configurable timeout (default 5 s)
  so the shell **never hangs** on a slow or unreachable server.
- `getaddrinfo()` resolves both IPv4 and IPv6 hosts.
- Output is human-readable by default; `--json` emits a stable schema.
- Registered in the AiShell registry and help system like all other commands.

```
Usage: net-get <host> [OPTIONS]
  --port <n>       TCP port (default: 80)
  --path <path>    request path (default: /)
  --timeout <sec>  connect/recv timeout (default: 5)
  --headers        print HTTP headers only
  --json           output in JSON format
```

---

## Part 2 — `mcp_server` (MCP server in C)

Listens on TCP port 9000.  Each connection carries **one JSON request**,
receives **one JSON response**, then closes — stateless and safe.

**Request format** (newline-terminated):
```json
{"tool": "ls", "args": ["/tmp"]}
```

**Response format**:
```json
{"tool": "ls", "exit_code": 0, "output": "file1\nfile2\n"}
```

**Special tool** — list all registered commands:
```json
{"tool": "__list__"}
→ {"tools": [{"name": "ls", "summary": "..."}, ...]}
```

Architecture: **fork-per-client** — each tool call runs in a child process
with stdout/stderr captured into a pipe and returned as JSON. The server
process itself stays clean even if a command crashes.

---

## Part 3 — Python Agent Notebook

`agent/week08_agent.ipynb` demonstrates:

1. `mcp_call()` — raw TCP client (socket / connect / send / recv)
2. `mcp_call_logged()` — logged wrapper (all calls appear in the log)
3. OpenAI tool schemas auto-generated from the MCP `__list__` response
4. `run_agent()` — full agent loop: NL → OpenAI → tool calls → MCP → answer
5. Three demo queries (system identity, filesystem, user/cwd)
6. `delete_older_than_days()` extension (dry-run safe by default)

**To run**:
```bash
export OPENAI_API_KEY=sk-...
pip install openai
# mcp_server must be running on port 9000
jupyter notebook agent/week08_agent.ipynb
```

---

## AI Work Log

| Step | What Claude Code did |
|------|----------------------|
| Branch | Created `week08` from `main` |
| Scaffold | Extracted all Week 06 source from git history into `week08/aishell/` |
| `net-get` | Wrote `cmd_netget.c` implementing the Session 8 socket client pattern with `SO_RCVTIMEO` timeouts, `getaddrinfo`, HTTP/1.0 GET, JSON output |
| Registry | Wired `net-get` into `register_all_commands.c` and `Makefile` |
| MCP server | Wrote `mcp_server.c`: JSON parser, fork-per-client, pipe-captured output, `__list__` endpoint, `SO_REUSEADDR` |
| Build | Both targets compile clean under `-Wall -Wextra -std=c11` |
| Smoke test | `net-get example.com` returned HTML; MCP server answered `__list__`, `echo`, `ls` tool calls correctly |
| Notebook | Created `week08_agent.ipynb` with architecture diagram, logged MCP client, OpenAI agent loop, 3 demo queries, `delete_older_than_days` extension, verification checklist |

---

## Verification Steps

```bash
# 1. Build
cd week08/aishell && make

# 2. net-get — socket client
./aishell net-get example.com --headers    # should print HTTP headers
./aishell net-get 127.0.0.1 --timeout 1   # should fail fast with timeout error

# 3. MCP server
./mcp_server &
echo '{"tool":"__list__"}' | nc -q1 127.0.0.1 9000 | python3 -m json.tool
echo '{"tool":"echo","args":["hello","MCP"]}' | nc -q1 127.0.0.1 9000
echo '{"tool":"pwd"}' | nc -q1 127.0.0.1 9000
echo '{"tool":"bad_cmd"}' | nc -q1 127.0.0.1 9000   # exit_code 127
echo '{}' | nc -q1 127.0.0.1 9000                    # error: invalid request
kill %1

# 4. Full acceptance checks (from Session 8 slides)
./aishell net-get --help                  # help registered in registry
./aishell help | grep net-get             # appears in command list
```

---

## Session 8 Acceptance Checks

| Criterion | Status |
|-----------|--------|
| Client command connects, exchanges data, handles errors | `net-get` — pass |
| Timeouts prevent hanging | `SO_RCVTIMEO/SNDTIMEO` — pass |
| Output usable by humans; `--json` has stable schema | pass |
| Help and registry consistent with other commands | pass |
| MCP server exposes tools as JSON | pass |
| Agent routes NL to MCP tool calls | notebook — pass |
| All MCP calls logged | `mcp_call_logged()` — pass |
| `delete_older_than_days` support | Python extension — pass |
