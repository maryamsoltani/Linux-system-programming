/*
 * net-get — minimal HTTP GET client command for aishell (Session 8)
 *
 * Usage: net-get <host> [port] [path] [--json] [--timeout <sec>]
 *
 * Pattern: socket() -> connect() -> send() -> recv() -> close()
 * Timeouts are enforced via SO_RCVTIMEO / SO_SNDTIMEO so the shell
 * never hangs on a non-responsive server.
 */

#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "cmd_spec.h"
#include "cmd_netget.h"
#include "json_utils.h"

#define DEFAULT_PORT     80
#define DEFAULT_PATH     "/"
#define DEFAULT_TIMEOUT  5          /* seconds */
#define RECV_BUF_SIZE    8192
#define MAX_RESPONSE     (64 * 1024)

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static int set_timeout(int fd, int seconds)
{
    struct timeval tv = { .tv_sec = seconds, .tv_usec = 0 };
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) return -1;
    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) return -1;
    return 0;
}

/* Resolve host and return a connected TCP socket fd, or -1 on error. */
static int tcp_connect(const char *host, int port, int timeout_sec)
{
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints = {
        .ai_family   = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res = NULL;

    int gai = getaddrinfo(host, port_str, &hints, &res);
    if (gai != 0) {
        fprintf(stderr, "net-get: cannot resolve '%s': %s\n",
                host, gai_strerror(gai));
        return -1;
    }

    int fd = -1;
    for (struct addrinfo *r = res; r != NULL; r = r->ai_next) {
        fd = socket(r->ai_family, r->ai_socktype, r->ai_protocol);
        if (fd < 0) continue;

        set_timeout(fd, timeout_sec);

        if (connect(fd, r->ai_addr, r->ai_addrlen) == 0) break;

        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd < 0)
        fprintf(stderr, "net-get: connect to %s:%d failed: %s\n",
                host, port, strerror(errno));
    return fd;
}

/* Send an HTTP/1.0 GET request and read the full response into *out.
 * Caller must free(*out). Returns response length or -1 on error.    */
static ssize_t http_get(int fd, const char *host, const char *path,
                         char **out)
{
    /* Build and send request */
    char req[1024];
    int reqlen = snprintf(req, sizeof(req),
        "GET %s HTTP/1.0\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "\r\n",
        path, host);

    if (send(fd, req, (size_t)reqlen, 0) < reqlen) {
        fprintf(stderr, "net-get: send failed: %s\n", strerror(errno));
        return -1;
    }

    /* Read response */
    char *buf = malloc(MAX_RESPONSE + 1);
    if (!buf) { perror("malloc"); return -1; }

    ssize_t total = 0;
    char chunk[RECV_BUF_SIZE];
    ssize_t n;

    while ((n = recv(fd, chunk, sizeof(chunk), 0)) > 0) {
        if (total + n > MAX_RESPONSE) n = MAX_RESPONSE - total;
        memcpy(buf + total, chunk, (size_t)n);
        total += n;
        if (total >= MAX_RESPONSE) break;
    }

    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            fprintf(stderr, "net-get: timeout waiting for response\n");
        else
            fprintf(stderr, "net-get: recv error: %s\n", strerror(errno));
        free(buf);
        return -1;
    }

    buf[total] = '\0';
    *out = buf;
    return total;
}

/* Strip HTTP headers; return pointer into buf just past \r\n\r\n. */
static const char *skip_headers(const char *buf)
{
    const char *body = strstr(buf, "\r\n\r\n");
    return body ? body + 4 : buf;
}

/* Extract first HTTP status line. */
static void parse_status(const char *buf, int *status_code, char *reason, size_t rlen __attribute__((unused)))
{
    *status_code = 0;
    reason[0] = '\0';
    if (strncmp(buf, "HTTP/", 5) == 0) {
        const char *sp = strchr(buf, ' ');
        if (sp) sscanf(sp + 1, "%d %127[^\r\n]", status_code, reason);
    }
}

/* ------------------------------------------------------------------ */
/* Command entry point                                                  */
/* ------------------------------------------------------------------ */

int netget_run(int argc, char **argv)
{
    const char *host        = NULL;
    int         port        = DEFAULT_PORT;
    const char *path        = DEFAULT_PATH;
    int         timeout_sec = DEFAULT_TIMEOUT;
    int         json_out    = 0;
    int         headers_only = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            netget_print_usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--json") == 0) {
            json_out = 1;
        } else if (strcmp(argv[i], "--headers") == 0) {
            headers_only = 1;
        } else if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) {
            timeout_sec = atoi(argv[++i]);
            if (timeout_sec <= 0) timeout_sec = DEFAULT_TIMEOUT;
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--path") == 0 && i + 1 < argc) {
            path = argv[++i];
        } else if (!host) {
            host = argv[i];
        }
    }

    if (!host) {
        fprintf(stderr, "net-get: host required\n");
        netget_print_usage(stderr);
        return 1;
    }

    int fd = tcp_connect(host, port, timeout_sec);
    if (fd < 0) return 1;

    char *response = NULL;
    ssize_t rlen = http_get(fd, host, path, &response);
    close(fd);

    if (rlen < 0) return 1;

    int status_code = 0;
    char reason[128] = {0};
    parse_status(response, &status_code, reason, sizeof(reason));
    const char *body = skip_headers(response);

    if (json_out) {
        printf("{\"command\":\"net-get\",\"host\":");
        json_print_string(stdout, host);
        printf(",\"port\":%d,\"path\":", port);
        json_print_string(stdout, path);
        printf(",\"status\":%d,\"reason\":", status_code);
        json_print_string(stdout, reason);
        printf(",\"body_bytes\":%zd,\"body\":", (ssize_t)(rlen - (body - response)));
        json_print_string(stdout, body);
        printf("}\n");
    } else if (headers_only) {
        /* print only up to end of headers */
        const char *end = strstr(response, "\r\n\r\n");
        if (end) fwrite(response, 1, (size_t)(end - response + 4), stdout);
        else     fputs(response, stdout);
    } else {
        fputs(body, stdout);
    }

    free(response);
    return (status_code >= 200 && status_code < 400) ? 0 : 1;
}

void netget_print_usage(FILE *out)
{
    fprintf(out, "Usage: net-get <host> [OPTIONS]\n");
    fprintf(out, "\nDescription:\n");
    fprintf(out, "  Send an HTTP GET request and print the response body.\n");
    fprintf(out, "  Implements: socket() -> connect() -> send()/recv() -> close()\n");
    fprintf(out, "\nOptions:\n");
    fprintf(out, "  %-26s %s\n", "-h, --help",    "show this help and exit");
    fprintf(out, "  %-26s %s\n", "--port <n>",     "TCP port (default: 80)");
    fprintf(out, "  %-26s %s\n", "--path <path>",  "request path (default: /)");
    fprintf(out, "  %-26s %s\n", "--timeout <sec>","connect/recv timeout (default: 5)");
    fprintf(out, "  %-26s %s\n", "--headers",      "print HTTP headers only");
    fprintf(out, "  %-26s %s\n", "--json",         "output in JSON format");
    fprintf(out, "\nExamples:\n");
    fprintf(out, "  net-get example.com\n");
    fprintf(out, "  net-get example.com --port 80 --path /index.html\n");
    fprintf(out, "  net-get example.com --json\n");
    fprintf(out, "  net-get example.com --headers\n");
}

static cmd_spec_t cmd_netget_spec = {
    .name        = "net-get",
    .summary     = "HTTP GET via raw TCP socket",
    .long_help   = "Send an HTTP/1.0 GET request using socket/connect/send/recv. "
                   "Enforces connect and receive timeouts.",
    .run         = netget_run,
    .print_usage = netget_print_usage,
};

void register_netget_command(void)
{
    register_command(&cmd_netget_spec);
}
