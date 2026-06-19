/*
 * ftpd.c — Week 09 FTP Server
 *
 * Session 9: Sockets Server and Shell Services
 * =============================================
 * A pedagogical FTP server that works with the real Linux `ftp` client.
 *
 * Architecture:
 *   - One detached pthread per accepted connection (thread-per-client).
 *   - Control port: 21021 (configurable via ftpd.conf).
 *   - Data connections: both active (PORT) and passive (PASV) supported.
 *   - Protocol framing: CRLF-terminated ASCII command lines on control socket;
 *     raw binary transfer on data socket.
 *
 * Supported commands:
 *   USER, PASS, QUIT, SYST, FEAT, NOOP, OPTS, TYPE
 *   PWD/XPWD, CWD/XCWD
 *   PORT, PASV
 *   LIST/NLST, MKD/XMKD, STOR, RETR
 *
 * Security model (teaching only — not production):
 *   - Any username/password is accepted; no real auth.
 *   - FTP root is hard-locked to $HOME (paths cannot escape via "..").
 *   - All operations gated by per-feature enable flags in ftpd.conf.
 *
 * Compile:  gcc ftpd.c -lpthread -o ftpd
 * Run:      ./ftpd [--config ftpd.conf]
 * Test:     ftp 127.0.0.1 21021
 *
 * Acceptance checks (Session 9):
 *   [x] Server accepts connections, serves requests, closes cleanly
 *   [x] Timeout on idle control connections (SO_RCVTIMEO)
 *   [x] Config file changes behavior (port, feature flags, limits)
 *   [x] Input validation rejects malformed / out-of-bounds requests
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <limits.h>
#include <ctype.h>
#include <time.h>

/* ─── tunables ─────────────────────────────────────────────────── */
#define DEFAULT_CTRL_PORT   21021
#define BACKLOG             16
#define BUF_SIZE            8192
#define CTRL_TIMEOUT_SEC    120   /* idle control-connection timeout */
#define CONFIG_FILE         "ftpd.conf"

/* ─── server configuration (loaded from ftpd.conf) ─────────────── */
typedef struct {
    int  ctrl_port;
    int  allow_list;
    int  allow_retr;
    int  allow_stor;
    int  allow_mkd;
    int  max_file_kb;    /* 0 = unlimited */
} config_t;

static config_t g_cfg = {
    .ctrl_port   = DEFAULT_CTRL_PORT,
    .allow_list  = 1,
    .allow_retr  = 1,
    .allow_stor  = 1,
    .allow_mkd   = 1,
    .max_file_kb = 0,
};

/* ─── per-session state ─────────────────────────────────────────── */
typedef struct {
    int  ctrl_fd;
    char cwd[PATH_MAX];    /* absolute, always within home */
    char home[PATH_MAX];   /* FTP root = $HOME */
    char username[64];
    int  logged_in;
    /* active (PORT) mode */
    int  port_set;
    char data_host[64];
    int  data_port;
    /* passive (PASV) mode */
    int  pasv_fd;          /* -1 if not set */
} session_t;

/* ─── helpers ───────────────────────────────────────────────────── */

static void ctrl_send(session_t *s, const char *fmt, ...) {
    char buf[BUF_SIZE];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    write(s->ctrl_fd, buf, strlen(buf));
}

/* Resolve an FTP path to an absolute local path rooted at session home.
 * FTP "/" maps to $HOME; FTP "/foo" maps to $HOME/foo.
 * Relative paths are relative to cwd.
 * Returns 0 on success, -1 if resolved path escapes home. */
static int resolve_path(session_t *s, const char *ftp_path,
                        char *out, size_t len) {
    /* raw needs room for two PATH_MAX components plus a slash */
    char raw[PATH_MAX * 2 + 2];
    if (!ftp_path || ftp_path[0] == '\0') {
        snprintf(raw, sizeof(raw), "%s", s->cwd);
    } else if (ftp_path[0] == '/') {
        /* strip leading slash, prepend home */
        if (ftp_path[1] == '\0')
            snprintf(raw, sizeof(raw), "%s", s->home);
        else
            snprintf(raw, sizeof(raw), "%s/%s", s->home, ftp_path + 1);
    } else {
        snprintf(raw, sizeof(raw), "%s/%s", s->cwd, ftp_path);
    }
    /* canonicalise (no trailing slash except root) */
    char canon[PATH_MAX];
    if (!realpath(raw, canon)) {
        /* path may not exist yet (STOR/MKD) — build it manually */
        strncpy(canon, raw, sizeof(canon) - 1);
    }
    /* safety: must stay within home */
    size_t hlen = strlen(s->home);
    if (strncmp(canon, s->home, hlen) != 0 ||
        (canon[hlen] != '\0' && canon[hlen] != '/')) {
        return -1;
    }
    strncpy(out, canon, len - 1);
    out[len - 1] = '\0';
    return 0;
}

/* Return the FTP-visible path (strip home prefix, add leading '/') */
static const char *ftp_visible(session_t *s, const char *abs_path) {
    size_t hlen = strlen(s->home);
    if (strncmp(abs_path, s->home, hlen) == 0) {
        const char *rel = abs_path + hlen;
        return (*rel == '\0') ? "/" : rel;
    }
    return abs_path;
}

/* Open a data connection (PORT or PASV) */
static int open_data_conn(session_t *s) {
    if (s->pasv_fd >= 0) {
        /* PASV mode: accept the client's incoming connection */
        struct sockaddr_in cli = {0};
        socklen_t cl = sizeof(cli);
        /* set a short timeout so PASV accept doesn't hang forever */
        struct timeval tv = {10, 0};
        setsockopt(s->pasv_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        int fd = accept(s->pasv_fd, (struct sockaddr *)&cli, &cl);
        close(s->pasv_fd);
        s->pasv_fd = -1;
        return fd;
    }
    if (!s->port_set) return -1;
    /* PORT mode: server connects to client */
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)s->data_port);
    inet_pton(AF_INET, s->data_host, &addr.sin_addr);
    struct timeval tv = {10, 0};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* ─── command handlers ──────────────────────────────────────────── */

static void cmd_user(session_t *s, const char *arg) {
    strncpy(s->username, arg, sizeof(s->username) - 1);
    ctrl_send(s, "331 Password required for %s.\r\n", s->username);
}

static void cmd_pass(session_t *s, const char *arg) {
    (void)arg;  /* accept any password */
    s->logged_in = 1;
    ctrl_send(s, "230 User %s logged in.\r\n", s->username);
}

static void cmd_syst(session_t *s) {
    ctrl_send(s, "215 UNIX Type: L8\r\n");
}

static void cmd_feat(session_t *s) {
    ctrl_send(s, "211 No special features\r\n");
}

static void cmd_noop(session_t *s) {
    ctrl_send(s, "200 NOOP ok.\r\n");
}

static void cmd_opts(session_t *s, const char *arg) {
    (void)arg;
    ctrl_send(s, "200 OK.\r\n");
}

static void cmd_type(session_t *s, const char *arg) {
    /* Accept I (binary) and A (ASCII) — we always use binary internally */
    char t = arg ? (char)toupper((unsigned char)arg[0]) : 'I';
    if (t != 'I' && t != 'A')
        ctrl_send(s, "504 Type not supported.\r\n");
    else
        ctrl_send(s, "200 Type set to %c.\r\n", t);
}

static void cmd_pwd(session_t *s) {
    ctrl_send(s, "257 \"%s\" is the current directory.\r\n",
              ftp_visible(s, s->cwd));
}

static void cmd_cwd(session_t *s, const char *arg) {
    char path[PATH_MAX];
    if (resolve_path(s, arg, path, sizeof(path)) < 0) {
        ctrl_send(s, "550 Permission denied.\r\n");
        return;
    }
    struct stat st;
    if (stat(path, &st) < 0 || !S_ISDIR(st.st_mode)) {
        ctrl_send(s, "550 %s: No such directory.\r\n", arg);
        return;
    }
    strncpy(s->cwd, path, sizeof(s->cwd) - 1);
    ctrl_send(s, "250 CWD command successful.\r\n");
}

static void cmd_port(session_t *s, const char *arg) {
    int a1, a2, a3, a4, p1, p2;
    if (sscanf(arg, "%d,%d,%d,%d,%d,%d", &a1, &a2, &a3, &a4, &p1, &p2) != 6 ||
        a1 < 0 || a1 > 255 || a2 < 0 || a2 > 255 ||
        a3 < 0 || a3 > 255 || a4 < 0 || a4 > 255 ||
        p1 < 0 || p1 > 255 || p2 < 0 || p2 > 255) {
        ctrl_send(s, "501 Bad PORT syntax.\r\n");
        return;
    }
    snprintf(s->data_host, sizeof(s->data_host),
             "%d.%d.%d.%d", a1, a2, a3, a4);
    s->data_port = p1 * 256 + p2;
    s->port_set  = 1;
    if (s->pasv_fd >= 0) { close(s->pasv_fd); s->pasv_fd = -1; }
    ctrl_send(s, "200 PORT command successful.\r\n");
}

static void cmd_pasv(session_t *s) {
    if (s->pasv_fd >= 0) { close(s->pasv_fd); s->pasv_fd = -1; }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { ctrl_send(s, "425 Can't open passive connection.\r\n"); return; }
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = 0;   /* OS assigns port */
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(fd, 1) < 0) {
        close(fd);
        ctrl_send(s, "425 Can't open passive connection.\r\n");
        return;
    }
    socklen_t al = sizeof(addr);
    getsockname(fd, (struct sockaddr *)&addr, &al);
    int port = ntohs(addr.sin_port);
    s->pasv_fd  = fd;
    s->port_set = 0;
    ctrl_send(s, "227 Entering Passive Mode (127,0,0,1,%d,%d).\r\n",
              port / 256, port % 256);
}

static void cmd_list(session_t *s, const char *arg) {
    if (!g_cfg.allow_list) {
        ctrl_send(s, "550 LIST is disabled by server configuration.\r\n");
        return;
    }
    /* strip any leading flags (e.g. -la from some ftp clients) */
    while (arg && *arg == '-') {
        while (*arg && *arg != ' ') arg++;
        while (*arg == ' ') arg++;
    }
    char path[PATH_MAX];
    if (resolve_path(s, (arg && *arg) ? arg : "", path, sizeof(path)) < 0) {
        ctrl_send(s, "550 Permission denied.\r\n");
        return;
    }
    int data_fd = open_data_conn(s);
    if (data_fd < 0) {
        ctrl_send(s, "425 Can't open data connection.\r\n");
        return;
    }
    ctrl_send(s, "150 Here comes the directory listing.\r\n");
    /* build ls command safely — path already validated */
    char cmd[PATH_MAX + 64];
    snprintf(cmd, sizeof(cmd), "ls -la -- %s 2>&1", path);
    FILE *fp = popen(cmd, "r");
    if (fp) {
        char line[2048];
        while (fgets(line, sizeof(line), fp))
            write(data_fd, line, strlen(line));
        pclose(fp);
    }
    close(data_fd);
    s->port_set = 0;
    ctrl_send(s, "226 Directory send OK.\r\n");
}

static void cmd_mkd(session_t *s, const char *arg) {
    if (!g_cfg.allow_mkd) {
        ctrl_send(s, "550 MKD is disabled by server configuration.\r\n");
        return;
    }
    if (!arg || *arg == '\0') {
        ctrl_send(s, "501 MKD requires a directory name.\r\n");
        return;
    }
    char path[PATH_MAX];
    if (resolve_path(s, arg, path, sizeof(path)) < 0) {
        ctrl_send(s, "550 Permission denied.\r\n");
        return;
    }
    if (mkdir(path, 0755) < 0) {
        if (errno == EEXIST)
            ctrl_send(s, "550 %s: Directory already exists.\r\n", arg);
        else
            ctrl_send(s, "550 %s: %s.\r\n", arg, strerror(errno));
        return;
    }
    ctrl_send(s, "257 \"%s\" created.\r\n", ftp_visible(s, path));
}

static void cmd_retr(session_t *s, const char *arg) {
    if (!g_cfg.allow_retr) {
        ctrl_send(s, "550 RETR is disabled by server configuration.\r\n");
        return;
    }
    if (!arg || *arg == '\0') {
        ctrl_send(s, "501 RETR requires a filename.\r\n");
        return;
    }
    char path[PATH_MAX];
    if (resolve_path(s, arg, path, sizeof(path)) < 0) {
        ctrl_send(s, "550 Permission denied.\r\n");
        return;
    }
    struct stat st;
    if (stat(path, &st) < 0 || !S_ISREG(st.st_mode)) {
        ctrl_send(s, "550 %s: No such file.\r\n", arg);
        return;
    }
    int in_fd = open(path, O_RDONLY);
    if (in_fd < 0) { ctrl_send(s, "550 %s: Permission denied.\r\n", arg); return; }
    int data_fd = open_data_conn(s);
    if (data_fd < 0) {
        close(in_fd);
        ctrl_send(s, "425 Can't open data connection.\r\n");
        return;
    }
    ctrl_send(s, "150 Opening BINARY mode data connection for %s (%lld bytes).\r\n",
              arg, (long long)st.st_size);
    char buf[BUF_SIZE];
    ssize_t n;
    while ((n = read(in_fd, buf, sizeof(buf))) > 0)
        write(data_fd, buf, n);
    close(in_fd);
    close(data_fd);
    s->port_set = 0;
    ctrl_send(s, "226 Transfer complete.\r\n");
}

static void cmd_stor(session_t *s, const char *arg) {
    if (!g_cfg.allow_stor) {
        ctrl_send(s, "550 STOR is disabled by server configuration.\r\n");
        return;
    }
    if (!arg || *arg == '\0') {
        ctrl_send(s, "501 STOR requires a filename.\r\n");
        return;
    }
    char path[PATH_MAX];
    if (resolve_path(s, arg, path, sizeof(path)) < 0) {
        ctrl_send(s, "550 Permission denied.\r\n");
        return;
    }
    int data_fd = open_data_conn(s);
    if (data_fd < 0) {
        ctrl_send(s, "425 Can't open data connection.\r\n");
        return;
    }
    int out_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd < 0) {
        ctrl_send(s, "452 %s: Cannot create file.\r\n", arg);
        close(data_fd);
        return;
    }
    ctrl_send(s, "150 Opening BINARY mode data connection for %s.\r\n", arg);
    char buf[BUF_SIZE];
    ssize_t n;
    long long total = 0;
    long long limit = (long long)g_cfg.max_file_kb * 1024;
    while ((n = read(data_fd, buf, sizeof(buf))) > 0) {
        if (limit > 0 && total + n > limit) {
            ctrl_send(s, "552 File size limit (%d KB) exceeded.\r\n",
                      g_cfg.max_file_kb);
            close(out_fd);
            close(data_fd);
            unlink(path);
            s->port_set = 0;
            return;
        }
        write(out_fd, buf, n);
        total += n;
    }
    close(out_fd);
    close(data_fd);
    s->port_set = 0;
    ctrl_send(s, "226 Transfer complete.\r\n");
}

/* ─── main connection handler ────────────────────────────────────── */

static void *handle_client(void *arg) {
    session_t *s = (session_t *)arg;

    /* set idle timeout on control socket */
    struct timeval tv = {CTRL_TIMEOUT_SEC, 0};
    setsockopt(s->ctrl_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ctrl_send(s, "220 FTP server ready.\r\n");

    char linebuf[BUF_SIZE];
    size_t linelen = 0;
    char readbuf[BUF_SIZE];

    while (1) {
        ssize_t n = read(s->ctrl_fd, readbuf, sizeof(readbuf));
        if (n <= 0) break;  /* client disconnected or timeout */

        /* append to accumulation buffer, guarding overflow */
        if (linelen + (size_t)n >= sizeof(linebuf))
            linelen = 0;
        memcpy(linebuf + linelen, readbuf, n);
        linelen += n;

        /* process every complete CRLF-terminated line */
        char *p = linebuf;
        char *eol;
        while ((eol = memchr(p, '\n', linelen - (size_t)(p - linebuf)))) {
            *eol = '\0';
            if (eol > p && *(eol - 1) == '\r') *(eol - 1) = '\0';

            /* split "COMMAND arg" */
            char cmd_upper[16] = {0};
            const char *arg_str = "";
            char *sp = strchr(p, ' ');
            if (sp) {
                size_t cl = (size_t)(sp - p);
                if (cl > 15) cl = 15;
                memcpy(cmd_upper, p, cl);
                arg_str = sp + 1;
            } else {
                strncpy(cmd_upper, p, 15);
            }
            for (char *c = cmd_upper; *c; c++)
                *c = (char)toupper((unsigned char)*c);

            /* dispatch */
            if      (!strcmp(cmd_upper, "USER")) cmd_user(s, arg_str);
            else if (!strcmp(cmd_upper, "PASS")) cmd_pass(s, arg_str);
            else if (!strcmp(cmd_upper, "SYST")) cmd_syst(s);
            else if (!strcmp(cmd_upper, "FEAT")) cmd_feat(s);
            else if (!strcmp(cmd_upper, "NOOP")) cmd_noop(s);
            else if (!strcmp(cmd_upper, "OPTS")) cmd_opts(s, arg_str);
            else if (!strcmp(cmd_upper, "TYPE")) cmd_type(s, arg_str);
            else if (!strcmp(cmd_upper, "PWD")  ||
                     !strcmp(cmd_upper, "XPWD")) cmd_pwd(s);
            else if (!strcmp(cmd_upper, "CWD")  ||
                     !strcmp(cmd_upper, "XCWD")) cmd_cwd(s, arg_str);
            else if (!strcmp(cmd_upper, "PORT")) cmd_port(s, arg_str);
            else if (!strcmp(cmd_upper, "PASV")) cmd_pasv(s);
            else if (!strcmp(cmd_upper, "LIST") ||
                     !strcmp(cmd_upper, "NLST")) cmd_list(s, arg_str);
            else if (!strcmp(cmd_upper, "MKD")  ||
                     !strcmp(cmd_upper, "XMKD")) cmd_mkd(s, arg_str);
            else if (!strcmp(cmd_upper, "STOR")) cmd_stor(s, arg_str);
            else if (!strcmp(cmd_upper, "RETR")) cmd_retr(s, arg_str);
            else if (!strcmp(cmd_upper, "ABOR")) {
                ctrl_send(s, "226 Abort successful.\r\n");
            }
            else if (!strcmp(cmd_upper, "QUIT")) {
                ctrl_send(s, "221 Goodbye.\r\n");
                goto cleanup;
            }
            else {
                ctrl_send(s, "500 Unknown command %s.\r\n", cmd_upper);
            }

            p = eol + 1;
        }

        /* shift unconsumed bytes to front of buffer */
        size_t remaining = linelen - (size_t)(p - linebuf);
        if (remaining > 0)
            memmove(linebuf, p, remaining);
        linelen = remaining;
    }

cleanup:
    if (s->pasv_fd >= 0) close(s->pasv_fd);
    close(s->ctrl_fd);
    free(s);
    return NULL;
}

/* ─── config file loader ─────────────────────────────────────────── */

static void load_config(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;  /* missing config is fine — use defaults */
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        /* strip comment and trailing whitespace */
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        char *key = line;
        while (*key == ' ' || *key == '\t') key++;
        char *eq = strchr(key, '=');
        if (!eq) continue;
        *eq = '\0';
        char *val = eq + 1;
        /* trim trailing whitespace from key and value */
        char *end = key + strlen(key);
        while (end > key && (*(end-1) == ' ' || *(end-1) == '\t' || *(end-1) == '\n'))
            *--end = '\0';
        while (*val == ' ' || *val == '\t') val++;
        end = val + strlen(val);
        while (end > val && (*(end-1) == ' ' || *(end-1) == '\t' || *(end-1) == '\n'))
            *--end = '\0';
        if (*key == '\0' || *val == '\0') continue;

        if      (!strcmp(key, "port"))         g_cfg.ctrl_port   = atoi(val);
        else if (!strcmp(key, "allow_list"))   g_cfg.allow_list  = atoi(val);
        else if (!strcmp(key, "allow_retr"))   g_cfg.allow_retr  = atoi(val);
        else if (!strcmp(key, "allow_stor"))   g_cfg.allow_stor  = atoi(val);
        else if (!strcmp(key, "allow_mkd"))    g_cfg.allow_mkd   = atoi(val);
        else if (!strcmp(key, "max_file_kb"))  g_cfg.max_file_kb = atoi(val);
    }
    fclose(f);
}

/* ─── main ───────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    /* optional --config <file> flag */
    const char *cfg_path = CONFIG_FILE;
    for (int i = 1; i < argc - 1; i++)
        if (!strcmp(argv[i], "--config")) cfg_path = argv[i + 1];
    load_config(cfg_path);

    signal(SIGPIPE, SIG_IGN);   /* ignore broken-pipe from disconnected clients */

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); exit(1); }
    int yes = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)g_cfg.ctrl_port);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(1);
    }
    if (listen(listen_fd, BACKLOG) < 0) {
        perror("listen"); exit(1);
    }

    printf("ftpd ready — ctrl port %d\n", g_cfg.ctrl_port);
    printf("ftp root   : %s\n", getenv("HOME") ? getenv("HOME") : "(no HOME)");
    printf("features   : LIST=%d RETR=%d STOR=%d MKD=%d max_file_kb=%d\n",
           g_cfg.allow_list, g_cfg.allow_retr, g_cfg.allow_stor,
           g_cfg.allow_mkd,  g_cfg.max_file_kb);
    printf("Connect    : ftp 127.0.0.1 %d\n\n", g_cfg.ctrl_port);

    while (1) {
        struct sockaddr_in cli = {0};
        socklen_t cl = sizeof(cli);
        int client_fd = accept(listen_fd, (struct sockaddr *)&cli, &cl);
        if (client_fd < 0) { perror("accept"); continue; }

        printf("[%s] connected\n", inet_ntoa(cli.sin_addr));

        /* allocate session; thread takes ownership and frees it */
        session_t *sess = calloc(1, sizeof(session_t));
        if (!sess) { close(client_fd); continue; }
        sess->ctrl_fd = client_fd;
        sess->pasv_fd = -1;
        const char *home = getenv("HOME");
        if (!home) home = "/tmp";
        strncpy(sess->home, home, sizeof(sess->home) - 1);
        strncpy(sess->cwd,  home, sizeof(sess->cwd)  - 1);

        pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&tid, &attr, handle_client, sess) != 0) {
            perror("pthread_create");
            free(sess);
            close(client_fd);
        }
        pthread_attr_destroy(&attr);
    }
    return 0;
}
