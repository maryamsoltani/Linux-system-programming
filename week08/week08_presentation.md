# Week 08 — Sockets (Client) + MCP Agent Integration
### EMB.X 420 & 425 — Linux System Programming
**AiShell: From Shell to AI-Connected Tool Server**

---

## Slide 1 — What We Built This Week

Three connected pieces:

| Part | What | Language |
|------|------|----------|
| `net-get` | HTTP GET via raw TCP socket | C |
| `mcp_server` | JSON tool server over TCP port 9000 | C |
| `week08_agent.ipynb` | AI agent that calls the C server | Python |

**The full pipeline:**
```
User (NL) → OpenAI Agent → MCP tool call → C server → AiShell command → JSON result → Agent → Answer
```

---

## Slide 2 — Why Sockets?

Raw LLMs lack operational grounding:

| Problem | Consequence |
|---------|-------------|
| Hallucination | Invents commands that don't exist |
| No awareness | Doesn't know what's installed |
| Syntax errors | Generates broken CLI commands |
| Unsafe execution | Proposes risky commands without checks |

**Solution:** RAG + MCP — give the AI *real* tools backed by verified C code.

---

## Slide 3 — Socket Client Pattern (Session 8)

The `net-get` command implements the core client pattern:

```
socket() → connect() → send() → recv() → close()
```

```c
// 1. Resolve host
getaddrinfo(host, port_str, &hints, &res);

// 2. Create and connect socket
fd = socket(AF_INET, SOCK_STREAM, 0);
connect(fd, r->ai_addr, r->ai_addrlen);

// 3. Send HTTP request
send(fd, request, reqlen, 0);

// 4. Receive response
recv(fd, chunk, sizeof(chunk), 0);

// 5. Close
close(fd);
```

**Critical:** `SO_RCVTIMEO` + `SO_SNDTIMEO` enforce timeouts — the shell never hangs.

---

## Slide 4 — `net-get` Demo

```bash
# Basic usage — print HTML body
./aishell net-get example.com

# Show HTTP headers only
./aishell net-get example.com --headers

# Machine-readable JSON output
./aishell net-get example.com --json

# Custom port and path
./aishell net-get example.com --port 80 --path /index.html

# Fast fail on unreachable host
./aishell net-get 10.0.0.1 --timeout 2
```

JSON output schema (stable):
```json
{
  "command": "net-get",
  "host": "example.com",
  "port": 80,
  "status": 200,
  "reason": "OK",
  "body_bytes": 1256,
  "body": "<!doctype html>..."
}
```

---

## Slide 5 — What is MCP?

**Model Context Protocol** — a standard interface connecting AI models to tools.

```
AI Model ←→ MCP Server ←→ Real-World Tools / Devices
```

Think of it as a **"USB standard" for AI tools**:
- MCP Server exposes tools with: name, description, parameters, output format
- MCP Client (AI) can: discover tools, call tools, receive structured results
- Swap AI models without rewriting the integration code

In our project:
```
OpenAI Agent ←→ Python MCP client ←→ mcp_server (C) ←→ AiShell commands
```

---

## Slide 6 — MCP Server Architecture

```
┌──────────────────────────────────────────┐
│            mcp_server (C)                │
│                                          │
│  listen(port 9000)                       │
│       │                                  │
│  accept() ──→ fork()                     │
│                  │                       │
│            handle_client()               │
│                  │                       │
│           parse JSON request             │
│                  │                       │
│           find_command(tool)             │
│                  │                       │
│           pipe() + fork()                │
│                  │                       │
│           cmd->run(argc, argv)           │
│           (stdout captured via pipe)     │
│                  │                       │
│           return JSON response           │
└──────────────────────────────────────────┘
```

**Fork-per-client** — each tool call runs in isolation. A crash in one command cannot corrupt the server.

---

## Slide 7 — MCP Protocol

**Request** (newline-terminated JSON):
```json
{"tool": "ls", "args": ["/tmp"]}
```

**Response**:
```json
{"tool": "ls", "exit_code": 0, "output": "file1\nfile2\n"}
```

**Discovery** — list all tools:
```json
{"tool": "__list__"}
```
```json
{"tools": [
  {"name": "ls",      "summary": "list directory contents"},
  {"name": "echo",    "summary": "print text to stdout"},
  {"name": "net-get", "summary": "HTTP GET via raw TCP socket"},
  ...
]}
```

---

## Slide 8 — MCP Server Demo

```bash
# Terminal 1: start the server
cd week08/aishell
./mcp_server
# → mcp_server: listening on port 9000

# Terminal 2: call tools manually
echo '{"tool":"__list__"}' | nc -q1 127.0.0.1 9000

echo '{"tool":"echo","args":["hello","from","MCP"]}' | nc -q1 127.0.0.1 9000
# → {"tool":"echo","exit_code":0,"output":"hello from MCP\n"}

echo '{"tool":"ls","args":["/tmp"]}' | nc -q1 127.0.0.1 9000
# → {"tool":"ls","exit_code":0,"output":"file1\nfile2\n..."}

echo '{"tool":"bad_cmd"}' | nc -q1 127.0.0.1 9000
# → {"tool":"bad_cmd","exit_code":127,"error":"command not found"}
```

---

## Slide 9 — Traditional RAG vs Agentic RAG

| Traditional RAG | Agentic RAG (our approach) |
|-----------------|---------------------------|
| Linear: query → retrieve → generate | Iterative loop with tool use |
| One-shot | Plans, retries, validates |
| Static context | Dynamic: calls real tools |
| No execution | Executes shell commands via MCP |

**Our system is Agentic RAG:**
```
User Query
    ↓
OpenAI Agent (reason + plan)
    ↓
MCP tool call (structured)
    ↓
C shell command (real execution)
    ↓
JSON result (grounded fact)
    ↓
Agent synthesizes answer
```

---

## Slide 10 — Python Agent

```python
# 1. Discover tools from the C server
tool_list = mcp_call("__list__")["tools"]

# 2. Build OpenAI function schemas automatically
openai_tools = build_openai_tools(tool_list)

# 3. Run agent loop
def run_agent(query):
    messages = [system_prompt, user_query]
    while True:
        resp = client.chat.completions.create(
            model="gpt-4o-mini",
            messages=messages,
            tools=openai_tools,
            tool_choice="auto"
        )
        if no tool_calls:
            return final_answer
        for tool_call:
            result = mcp_call_logged(tool, args)  # logged!
            messages.append(tool_result)
```

---

## Slide 11 — Agent Demo (3 NL Queries)

```
Q: What operating system and architecture am I running on?
→ [calls uname via MCP]
A: You are running Linux on x86_64 architecture.

Q: List the files in /tmp and tell me how many there are.
→ [calls ls /tmp via MCP]
A: There are 14 files in /tmp, including snap-private-tmp,
   systemd-private-*, and mcp-ZIsHAD.

Q: Who am I logged in as, and what is my current working directory?
→ [calls whoami, then pwd via MCP]
A: You are logged in as marmar. Your current working
   directory is /home/marmar/project/week08/aishell.
```

All calls are logged:
```
2026-06-04 22:15:01  CALL  tool=uname  args=[]
2026-06-04 22:15:01  RESULT exit=0  output='Linux...'
```

---

## Slide 12 — Safety Layer

The MCP server enforces safety through architecture:

| Concern | How we address it |
|---------|------------------|
| Command injection | Only registered commands can be called — no `system()` or raw shell |
| Crash isolation | Fork-per-client: a bad command cannot kill the server |
| Output overflow | `MAX_OUTPUT = 64KB` cap on captured output |
| Timeouts | `net-get` enforces `SO_RCVTIMEO`; Python client has `socket.timeout` |
| Sensitive data | API keys in environment variables only — never in source |

---

## Slide 13 — Project Structure

```
week08/
├── README.md              project overview + verification steps
├── AGENT.md               AI assistant guide (full project)
├── aishell/
│   ├── Makefile           builds aishell + mcp_server
│   ├── AGENT.md           AI assistant guide (C codebase)
│   ├── cmd_netget.c       net-get socket client command
│   ├── mcp_server.c       MCP TCP server
│   └── cmd_*.c            23 original AiShell commands
└── agent/
    └── week08_agent.ipynb OpenAI agent → MCP → C shell
```

Build:
```bash
cd week08/aishell && make
# produces: ./aishell   ./mcp_server
```

---

## Slide 14 — Acceptance Checklist

| Criterion | Status |
|-----------|--------|
| Socket client connects, exchanges data, handles errors | `net-get` ✅ |
| Timeouts prevent shell from hanging | `SO_RCVTIMEO/SNDTIMEO` ✅ |
| Output usable by humans; `--json` has stable schema | ✅ |
| Help and registry consistent with other commands | ✅ |
| MCP server exposes tools as JSON | ✅ |
| Agent routes NL queries to MCP tool calls | ✅ |
| All MCP calls are logged | `mcp_call_logged()` ✅ |
| `delete_older_than_days` extension | Python dry-run ✅ |
| Demo artifact + AI work log + verification steps | README.md ✅ |

---

## Slide 15 — Key Takeaways

**`net-get`**
> The Session 8 socket pattern (`socket→connect→send/recv→close`) is the
> foundation of all networked software. Timeouts are non-negotiable.

**`mcp_server`**
> MCP decouples the AI reasoning layer from the execution layer.
> Fork-per-client is simple, safe, and isolates failures.

**Agent notebook**
> An AI agent grounded in real tool calls is more reliable than one
> answering from training data alone — this is the core idea of RAG.

**The bigger picture**
> Shell → Socket → MCP → Agent: each week adds one layer.
> By week 08 the shell has become an AI-callable microservice.
