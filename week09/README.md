# Week 09 — Sockets Server + Shell Services (FTP)

Session 9 extends the socket work from Week 08 in two directions:

1. **`ftpd`** — a real, RFC-compatible FTP server in C, usable with the system `ftp` client.
2. **`agent/week09_mcp_agent.ipynb`** — a Python notebook bridging the MCP AI pattern
   to a new C server, building on the Week 08 MCP concept.

---

## Project layout

```
week09/
  ftpd/
    ftpd.c          ← FTP server (thread-per-client with pthreads)
    ftpd.conf       ← configuration file (port, feature flags, limits)
    Makefile        ← builds `ftpd`
  agent/
    week09_mcp_agent.ipynb  ← MCP-like server in C + Python agent
  README.md
  AGENT.md
```

---

## Quick start

```bash
cd week09/ftpd

# Build
make

# Run (reads ftpd.conf from current directory)
./ftpd

# Connect with the standard Linux FTP client
ftp 127.0.0.1 21021
```

---

## Part 1 — `ftpd` (FTP server)

Implements the FTP protocol subset required by Session 9.

### Architecture

```
main()
  socket() → bind() → listen()
  loop: accept()
    calloc session_t
    pthread_create (DETACHED) → handle_client()
      SO_RCVTIMEO on ctrl socket
      220 greeting
      command loop: read lines → dispatch
      QUIT → cleanup → thread exits
```

**Concurrency model:** each accepted connection gets a detached `pthread`.
The main thread never blocks waiting for a client; it immediately loops back
to `accept()`. This satisfies the Session 9 requirement for concurrent clients.

### Data connections

Two modes are supported so the server works with both `ftp` (passive) and
`ftp -A` (active):

| Mode | Direction | When used |
|------|-----------|-----------|
| **PORT** (active) | Server → client | Client sends `PORT a1,a2,a3,a4,p1,p2` |
| **PASV** (passive) | Client → server | Client sends `PASV`; server picks an ephemeral port |

### Supported commands

| Command | Description |
|---------|-------------|
| `USER` / `PASS` | Login (any credentials accepted) |
| `SYST` / `FEAT` / `NOOP` / `OPTS` | Handshake helpers |
| `TYPE I` / `TYPE A` | Transfer type (binary / ASCII) |
| `PWD` / `CWD` | Print / change working directory |
| `PORT` / `PASV` | Select data connection mode |
| `LIST` / `NLST` | Directory listing over data connection |
| `MKD` / `XMKD` | Create directory |
| `STOR` | Upload a file (binary, handles NUL bytes) |
| `RETR` | Download a file (binary, handles NUL bytes) |
| `QUIT` | Close connection (server keeps running) |
| `ABOR` | Abort (acknowledged) |

### FTP root = `$HOME`

All paths are rooted at the user's `$HOME` directory. A client requesting `/foo`
is served `$HOME/foo`. Paths containing `..` that try to escape `$HOME` are
rejected with `550 Permission denied`.

### Operational requirements (Session 9 acceptance checks)

| Check | Implementation |
|-------|----------------|
| Accepts connections, serves requests, closes cleanly | ✓ pthreads, 221 on QUIT |
| Timeouts prevent hanging | ✓ `SO_RCVTIMEO` (120 s) on ctrl; 10 s on data |
| Configuration changes behavior | ✓ `ftpd.conf` — port, feature flags, size limit |
| Input validation prevents crashes on malformed requests | ✓ `PORT` parser validates 6 integers; path escapes rejected |

### Configuration (`ftpd.conf`)

```ini
port        = 21021    # control port
allow_list  = 1        # set 0 to disable LIST
allow_retr  = 1        # set 0 to disable RETR (download)
allow_stor  = 1        # set 0 to disable STOR (upload)
allow_mkd   = 1        # set 0 to disable MKD (mkdir)
max_file_kb = 0        # 0 = unlimited; >0 caps STOR file size
```

---

## Part 2 — MCP Agent Notebook

`agent/week09_mcp_agent.ipynb` demonstrates:

1. A minimal MCP-like server written in C (`server.c`) — fork-per-client,
   newline-delimited JSON, tools: `list_files`, `get_time`, `delete_older_than_days`.
2. A Python client (`client.py`) performing `initialize` → `list_tools` → `call_tool`.
3. A mocked AI agent that picks a tool based on a natural-language request and
   forwards the `call_tool` to the MCP server.
4. Notes on integrating a real LLM via the OpenAI API or CLI.

**To run:**

```bash
# Build and start the MCP server (Week 08 mcp_server works too)
cd /mnt/data
gcc server.c -o mcp_server && ./mcp_server &

# Run the client
python3 client.py

# Or open the notebook
jupyter notebook agent/week09_mcp_agent.ipynb
```

---

## Test procedure (Session 9 assignment)

```bash
cd week09/ftpd
./ftpd &

ftp 127.0.0.1 21021
# ftp prompts for user/pass — any credentials work

ftp> mkdir testSplitName          # MKD → 257 created
ftp> mkdir testSplitName          # MKD → 550 already exists
ftp> put /etc/hostname            # STOR → 226 transfer complete
ftp> get hostname                 # RETR → 226 transfer complete
ftp> get nosuchfile               # RETR → 550 no such file

# Binary file with NUL bytes:
python3 -c "open('/tmp/nul.bin','wb').write(b'a\x00b\x00c')"
ftp> put /tmp/nul.bin nul.bin     # STOR
ftp> get nul.bin /tmp/nul_out.bin # RETR
# verify: diff /tmp/nul.bin /tmp/nul_out.bin

ftp> ls                           # LIST (no arg → home)
ftp> ls /                         # LIST / → same as home
ftp> ls testSplitName             # LIST subdir

ftp> quit                         # QUIT → 221, server keeps running
ftp 127.0.0.1 21021               # reconnect without restarting server
```

---

## AI Work Log

| Step | What Claude Code did |
|------|----------------------|
| Branch | Created `week09` from `main` |
| `ftpd.c` | Implemented full FTP server: pthread-per-client, PASV+PORT modes, USER/PASS/SYST/FEAT/TYPE/PWD/CWD/PORT/PASV/LIST/MKD/STOR/RETR/QUIT, NVT response codes, binary transfer, NUL-safe, path-escape protection |
| `ftpd.conf` | Added config file with port, feature flags, size limits |
| `Makefile` | Single-target makefile, zero warnings |
| Smoke test | `nc`-based: USER/PASS/MKD/duplicate-MKD/QUIT all returned correct codes |
| Binary test | Python test: PASV+STOR+RETR round-tripped a 16-byte NUL-containing file exactly |
| Notebook | Wrote `week09_mcp_agent.ipynb`: MCP-like C server, Python client, mocked agent, OpenAI integration notes, lab rubric, security table |

---

## Verification Steps

```bash
# 1. Build
cd week09/ftpd && make

# 2. Start server
./ftpd &

# 3. Quick smoke test (nc)
(printf "USER foo\r\nPASS x\r\nSYST\r\nQUIT\r\n"; sleep 0.3) | nc -q1 127.0.0.1 21021

# 4. Config test — disable STOR, restart, verify 550
#    Edit ftpd.conf: allow_stor = 0
#    kill %1; ./ftpd &; ftp> put /etc/hostname  → should get 550

# 5. Full test procedure (see above)

# 6. Reconnect test — quit ftp, reconnect without kill %1
kill %1 2>/dev/null
```

---

## Session 9 Acceptance Checks

| Criterion | Status |
|-----------|--------|
| Server accepts connection, serves ≥1 request, closes cleanly | `QUIT` → 221, thread exits — ✓ |
| Timeouts prevent hanging connections | `SO_RCVTIMEO` 120 s ctrl, 10 s data — ✓ |
| Configuration changes behavior | `ftpd.conf` feature flags disable commands — ✓ |
| Input validation prevents crashes on malformed requests | `PORT` validates 6 integers; path escapes rejected with 550 — ✓ |
