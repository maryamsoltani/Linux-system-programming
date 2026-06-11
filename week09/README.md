# Week 09 — Sockets Server + Shell Services (FTP)

Session 9 extends the socket work from Week 08 in two directions:

1. **`ftpd`** — a real FTP server in C, compatible with the Linux `ftp` client.
2. **`agent/week09_mcp_agent.ipynb`** — a Python notebook bridging the MCP AI
   pattern to a new C server, building on the Week 08 MCP concept.

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

## How to build and run

### 1. Build

```bash
cd week09/ftpd
make
```

Expected output:
```
gcc -Wall -Wextra -std=c11 -g -o ftpd ftpd.c -lpthread
```

### 2. Start the server

```bash
./ftpd
```

The server reads `ftpd.conf` from the current directory and prints:
```
ftpd ready — ctrl port 21021
ftp root   : /home/<you>
features   : LIST=1 RETR=1 STOR=1 MKD=1 max_file_kb=0
Connect    : ftp 127.0.0.1 21021
```

### 3. Connect with the FTP client

Open a second terminal:

```bash
ftp 127.0.0.1 21021
```

Enter **any** username and password when prompted (no real authentication).

### 4. Example FTP session

```
ftp> mkdir testdir          # create a directory  → 257 created
ftp> mkdir testdir          # duplicate           → 550 already exists

ftp> put /etc/hostname      # upload a file       → 226 transfer complete
ftp> get hostname           # download it back    → 226 transfer complete
ftp> get nosuchfile         # missing file        → 550 not found

ftp> ls                     # list home directory
ftp> ls /                   # list FTP root (same as home)
ftp> ls testdir             # list a subdirectory

ftp> quit                   # close session       → 221 Goodbye
                            # (server keeps running)
ftp 127.0.0.1 21021         # reconnect without restarting the server
```

### 5. Binary files with NUL bytes

```bash
# create a binary file containing NUL characters
python3 -c "open('/tmp/nul.bin','wb').write(b'a\x00b\x00c')"
```

```
ftp> put /tmp/nul.bin nul.bin       # STOR
ftp> get nul.bin /tmp/nul_out.bin   # RETR
```

```bash
# verify the round-trip is byte-perfect
diff /tmp/nul.bin /tmp/nul_out.bin && echo "OK"
```

### 6. Stop the server

```bash
kill %1          # if started in the background with &
# or Ctrl-C if running in the foreground
```

---

## Configuration (`ftpd.conf`)

Edit `ftpd.conf` and restart `ftpd` to apply changes.

```ini
port        = 21021    # control port
allow_list  = 1        # set 0 to disable LIST (directory listing)
allow_retr  = 1        # set 0 to disable RETR (download)
allow_stor  = 1        # set 0 to disable STOR (upload)
allow_mkd   = 1        # set 0 to disable MKD (mkdir)
max_file_kb = 0        # 0 = unlimited; >0 caps STOR file size in KB
```

Example — disable uploads and cap the port:

```bash
# edit ftpd.conf: allow_stor = 0
kill %1
./ftpd &
# ftp> put /etc/hostname  → now returns 550
```

---

## Part 1 — `ftpd` (FTP server)

### Architecture

```
main()
  socket() → bind(21021) → listen()
  loop: accept()
    calloc(session_t)
    pthread_create(DETACHED) → handle_client()
      SO_RCVTIMEO(120s) on ctrl socket
      send 220 greeting
      loop: read lines → parse command → dispatch
      QUIT / disconnect → cleanup → thread exits
```

**Concurrency:** each accepted connection gets a detached `pthread`.
The main thread loops back to `accept()` immediately — multiple clients
can connect at the same time.

### Data connection modes

| Mode | How it works |
|------|-------------|
| **PASV** (passive) | Server picks a random port, client connects to it. Default for modern `ftp`. |
| **PORT** (active) | Client tells server its address; server connects to client. Use `ftp -A`. |

Both modes are supported.

### Supported commands

| Command | Description |
|---------|-------------|
| `USER` / `PASS` | Login — any credentials accepted |
| `SYST` / `FEAT` / `NOOP` / `OPTS` | Handshake helpers |
| `TYPE I` / `TYPE A` | Transfer type (binary / ASCII) |
| `PWD` / `CWD` | Print / change working directory |
| `PORT` / `PASV` | Select data connection mode |
| `LIST` / `NLST` | Directory listing over data connection |
| `MKD` / `XMKD` | Create directory |
| `STOR` | Upload a file (binary-safe, handles NUL bytes) |
| `RETR` | Download a file (binary-safe, handles NUL bytes) |
| `QUIT` | Close connection — server keeps running |
| `ABOR` | Abort current transfer |

### FTP root = `$HOME`

All paths are rooted at the user's `$HOME`. A client requesting `/foo`
is served `$HOME/foo`. Paths that try to escape `$HOME` via `..` are
rejected with `550 Permission denied`.

### Session 9 acceptance checks

| Check | Implementation |
|-------|----------------|
| Accepts connections, serves requests, closes cleanly | pthreads, `221` on QUIT |
| Timeouts prevent hanging connections | `SO_RCVTIMEO` 120 s ctrl, 10 s data |
| Configuration changes behavior | `ftpd.conf` feature flags, port, size limit |
| Input validation prevents crashes | PORT validates 6 integers; path escapes rejected |

---

## Part 2 — MCP Agent Notebook

`agent/week09_mcp_agent.ipynb` demonstrates the MCP (Model Context Protocol)
pattern from the Session 9 MCP slides.

### What it contains

1. A minimal MCP-like server in C (`server.c`) — fork-per-client,
   newline-delimited JSON, tools: `list_files`, `get_time`, `delete_older_than_days`.
2. A Python client (`client.py`) — `initialize` → `list_tools` → `call_tool`.
3. A mocked AI agent loop — NL request → tool selection → MCP call → result.
4. OpenAI API / CLI integration notes for connecting a real LLM.

### How to run the notebook

```bash
# 1. Open the notebook
cd week09
jupyter notebook agent/week09_mcp_agent.ipynb

# 2. Run all cells
#    The notebook writes /mnt/data/server.c and /mnt/data/client.py

# 3. Compile and start the MCP server (in a terminal)
cd /mnt/data
gcc server.c -o mcp_server && ./mcp_server &

# 4. Run the Python client (in a terminal)
python3 client.py

# 5. Call the mocked agent (in the notebook)
agent_flow('List all source files in the current directory')
agent_flow('What time is it on the server?')
agent_flow('Delete files older than 30 days')
```

### Calling with a real LLM

```bash
# OpenAI CLI (requires OPENAI_API_KEY)
openai api chat.completions.create \
    --model gpt-4o \
    --mcp-server http://localhost:9000 \
    -m "List files and tell me the server time."
```

---

## Verification steps

```bash
cd week09/ftpd

# 1. Build
make

# 2. Start server
./ftpd &

# 3. Quick smoke test (no ftp client needed)
(printf "USER foo\r\nPASS x\r\nSYST\r\nPWD\r\nQUIT\r\n"; sleep 0.3) \
    | nc -q1 127.0.0.1 21021

# 4. Config test — disable uploads
#    Edit ftpd.conf: allow_stor = 0
#    kill %1 && ./ftpd &
#    ftp 127.0.0.1 21021 → put /etc/hostname → should get 550

# 5. Full test procedure (see "Example FTP session" above)

# 6. Reconnect test
#    Inside ftp: quit
#    ftp 127.0.0.1 21021 (reconnect — server must still be running)

kill %1
```

---

## AI Work Log

| Step | What Claude Code did |
|------|----------------------|
| Branch | Created `week09` from `main` |
| `ftpd.c` | Full FTP server: pthread-per-client, PASV+PORT, USER/PASS/SYST/FEAT/TYPE/PWD/CWD/PORT/PASV/LIST/MKD/STOR/RETR/QUIT, NVT response codes, binary transfer, NUL-safe, path-escape protection |
| `ftpd.conf` | Config file: port, feature flags, size limit |
| `Makefile` | Single-target makefile, zero warnings |
| Smoke test | `nc`-based: USER/PASS/MKD/duplicate-MKD/QUIT all returned correct codes |
| Binary test | Python test: PASV+STOR+RETR round-tripped a 16-byte NUL-containing file exactly |
| Notebook | `week09_mcp_agent.ipynb`: MCP-like C server, Python client, mocked agent, OpenAI notes, lab rubric, security table |

---

## Session 9 Acceptance Checks

| Criterion | Status |
|-----------|--------|
| Server accepts connection, serves ≥1 request, closes cleanly | QUIT → 221, thread exits — ✓ |
| Timeouts prevent hanging connections | SO_RCVTIMEO 120 s ctrl, 10 s data — ✓ |
| Configuration changes behavior | ftpd.conf feature flags disable commands — ✓ |
| Input validation prevents crashes on malformed requests | PORT validates 6 integers; path escapes → 550 — ✓ |
