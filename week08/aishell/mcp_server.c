/*
 * mcp_server — TCP MCP server for aishell (Session 8)
 *
 * Listens on TCP port 9000 (default).
 * Each connection receives one JSON tool-call request and returns
 * one JSON response, then the connection closes.
 *
 * Request format  (newline-terminated JSON):
 *   {"tool": "<name>", "args": ["arg1", "arg2", ...]}
 *
 * Response format (newline-terminated JSON):
 *   {"tool": "<name>", "exit_code": 0, "output": "..."}
 *   {"tool": "<name>", "exit_code": 1, "error": "..."}
 *
 * Tool list endpoint — send:
 *   {"tool": "__list__"}
 * Returns:
 *   {"tools": [{"name":"ls","summary":"..."}, ...]}
 *
 * Architecture: one process per connection (fork-per-client) so each
 * tool call runs in isolation and cannot corrupt server state.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE   700

#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cmd_spec.h"
#include "json_utils.h"

#define DEFAULT_PORT   9000
#define BACKLOG        8
#define MAX_REQUEST    (4 * 1024)
#define MAX_ARGS       64
#define MAX_ARG_LEN    512
#define MAX_OUTPUT     (64 * 1024)

void register_all_builtin_commands(void);

/* ------------------------------------------------------------------ */
/* Minimal JSON parser for {"tool":"...","args":["...",...]}            */
/* ------------------------------------------------------------------ */

/* Skip whitespace */
static const char *jskip(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

/* Parse a JSON string into buf (max buflen-1 chars). Returns ptr past closing '"'. */
static const char *jstring(const char *p, char *buf, size_t buflen)
{
    buf[0] = '\0';
    p = jskip(p);
    if (*p != '"') return p;
    p++;
    size_t i = 0;
    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            switch (*p) {
            case '"':  if (i+1<buflen) buf[i++]='"';  break;
            case '\\': if (i+1<buflen) buf[i++]='\\'; break;
            case 'n':  if (i+1<buflen) buf[i++]='\n'; break;
            case 't':  if (i+1<buflen) buf[i++]='\t'; break;
            case 'r':  if (i+1<buflen) buf[i++]='\r'; break;
            default:   if (i+1<buflen) buf[i++]=*p;   break;
            }
        } else {
            if (i+1 < buflen) buf[i++] = *p;
        }
        p++;
    }
    buf[i] = '\0';
    if (*p == '"') p++;
    return p;
}

/* Parse request JSON. Returns 0 on success. */
static int parse_request(const char *json,
                          char *tool, size_t tool_len,
                          char args[MAX_ARGS][MAX_ARG_LEN],
                          int *argc_out)
{
    tool[0] = '\0';
    *argc_out = 0;

    const char *p = jskip(json);
    if (*p != '{') return -1;
    p++;

    while (*p && *p != '}') {
        p = jskip(p);
        if (*p != '"') break;

        char key[64];
        p = jstring(p, key, sizeof(key));
        p = jskip(p);
        if (*p != ':') break;
        p++;
        p = jskip(p);

        if (strcmp(key, "tool") == 0) {
            p = jstring(p, tool, tool_len);
        } else if (strcmp(key, "args") == 0) {
            if (*p != '[') { p++; continue; }
            p++;
            while (*p && *p != ']') {
                p = jskip(p);
                if (*p == '"') {
                    if (*argc_out < MAX_ARGS) {
                        p = jstring(p, args[*argc_out], MAX_ARG_LEN);
                        (*argc_out)++;
                    }
                }
                p = jskip(p);
                if (*p == ',') p++;
            }
            if (*p == ']') p++;
        } else {
            /* skip unknown value */
            if (*p == '"') { char tmp[256]; p = jstring(p, tmp, sizeof(tmp)); }
            else { while (*p && *p != ',' && *p != '}') p++; }
        }

        p = jskip(p);
        if (*p == ',') p++;
    }
    return tool[0] ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* Tool listing helper (avoids nested functions / executable stack)     */
/* ------------------------------------------------------------------ */

struct list_ctx { int fd; int first; };

static void list_tool_cb(const cmd_spec_t *spec, void *ud)
{
    struct list_ctx *ctx = ud;
    if (!ctx->first) dprintf(ctx->fd, ",");
    ctx->first = 0;
    dprintf(ctx->fd, "{\"name\":\"%s\",\"summary\":\"%s\"}",
            spec->name, spec->summary);
}

/* ------------------------------------------------------------------ */
/* Handle one client connection                                         */
/* ------------------------------------------------------------------ */

static void handle_client(int client_fd)
{
    /* Read request (one newline-terminated line) */
    char req[MAX_REQUEST];
    ssize_t total = 0;
    while (total < (ssize_t)sizeof(req) - 1) {
        ssize_t n = read(client_fd, req + total, 1);
        if (n <= 0) break;
        total++;
        if (req[total - 1] == '\n') break;
    }
    req[total] = '\0';

    char tool[128];
    char args_store[MAX_ARGS][MAX_ARG_LEN];
    int  nargs = 0;

    if (parse_request(req, tool, sizeof(tool), args_store, &nargs) != 0) {
        dprintf(client_fd,
            "{\"error\":\"invalid request — expected {\\\"tool\\\":\\\"name\\\",\\\"args\\\":[...]}\"}\n");
        return;
    }

    /* __list__ pseudo-tool: enumerate registered commands */
    if (strcmp(tool, "__list__") == 0) {
        dprintf(client_fd, "{\"tools\":[");
        for_each_command(list_tool_cb, &(struct list_ctx){ .fd = client_fd, .first = 1 });
        dprintf(client_fd, "]}\n");
        return;
    }

    /* Look up the command */
    const cmd_spec_t *cmd = find_command(tool);
    if (!cmd) {
        dprintf(client_fd,
            "{\"tool\":\"%s\",\"exit_code\":127,\"error\":\"command not found\"}\n",
            tool);
        return;
    }

    /* Build argv: argv[0] = tool name, argv[1..] = args */
    char *argv[MAX_ARGS + 2];
    argv[0] = tool;
    for (int i = 0; i < nargs; i++) argv[i + 1] = args_store[i];
    argv[nargs + 1] = NULL;
    int full_argc = nargs + 1;

    /* Capture stdout by redirecting to a pipe */
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        dprintf(client_fd, "{\"tool\":\"%s\",\"exit_code\":1,\"error\":\"pipe failed\"}\n", tool);
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]); close(pipefd[1]);
        dprintf(client_fd, "{\"tool\":\"%s\",\"exit_code\":1,\"error\":\"fork failed\"}\n", tool);
        return;
    }

    if (pid == 0) {
        /* child: redirect stdout/stderr to pipe write end */
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        int rc = cmd->run(full_argc, argv);
        fflush(stdout);
        _exit(rc);
    }

    /* parent: read child output */
    close(pipefd[1]);
    char *out = malloc(MAX_OUTPUT + 1);
    if (!out) { close(pipefd[0]); waitpid(pid, NULL, 0); return; }

    ssize_t out_len = 0;
    char chunk[4096];
    ssize_t n;
    while ((n = read(pipefd[0], chunk, sizeof(chunk))) > 0) {
        if (out_len + n > MAX_OUTPUT) n = MAX_OUTPUT - out_len;
        memcpy(out + out_len, chunk, (size_t)n);
        out_len += n;
        if (out_len >= MAX_OUTPUT) break;
    }
    out[out_len] = '\0';
    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;

    /* Send JSON response */
    dprintf(client_fd, "{\"tool\":\"%s\",\"exit_code\":%d,\"output\":", tool, exit_code);
    /* Write JSON-encoded output directly to client fd via a temp FILE* */
    FILE *cf = fdopen(dup(client_fd), "w");
    if (cf) {
        json_print_string(cf, out);
        fprintf(cf, "}\n");
        fclose(cf);
    } else {
        dprintf(client_fd, "\"\"}\n");
    }

    free(out);
}

/* ------------------------------------------------------------------ */
/* Main: create listening socket, accept loop                           */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    int port = DEFAULT_PORT;
    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "--port") == 0 || strcmp(argv[i], "-p") == 0)
            && i + 1 < argc) {
            port = atoi(argv[++i]);
        }
    }

    register_all_builtin_commands();

    /* Reap children automatically */
    struct sigaction sa = { .sa_handler = SIG_DFL, .sa_flags = SA_NOCLDWAIT };
    sigaction(SIGCHLD, &sa, NULL);

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return 1; }

    int one = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in sin = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port        = htons((uint16_t)port),
    };

    if (bind(srv, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        perror("bind"); return 1;
    }
    if (listen(srv, BACKLOG) < 0) {
        perror("listen"); return 1;
    }

    fprintf(stderr, "mcp_server: listening on port %d\n", port);
    fprintf(stderr, "  Protocol: {\"tool\":\"<name>\",\"args\":[\"...\"]}\n");
    fprintf(stderr, "  List tools: {\"tool\":\"__list__\"}\n");

    for (;;) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int cfd = accept(srv, (struct sockaddr *)&client_addr, &addr_len);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork"); close(cfd); continue;
        }
        if (pid == 0) {
            close(srv);
            handle_client(cfd);
            close(cfd);
            _exit(0);
        }
        close(cfd);
    }
}
