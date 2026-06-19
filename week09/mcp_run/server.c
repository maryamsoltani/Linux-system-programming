/*
 * server.c — Minimal MCP-like TCP server (Week 09)
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <ctype.h>

#define PORT    9000
#define BACKLOG 4
#define BUFSIZE 16384
#define MAX_ARG 512

int send_json(int fd, const char *json) {
    size_t len = strlen(json);
    if (write(fd, json, len) != (ssize_t)len) return -1;
    if (write(fd, "\n", 1) != 1) return -1;
    return 0;
}

void sanitize_path(const char *in, char *out, size_t outsz) {
    size_t i = 0;
    for (; *in && i + 1 < outsz; ++in) {
        if (isalnum((unsigned char)*in) ||
            *in == '/' || *in == '.' ||
            *in == '_' || *in == '-' || *in == ' ')
            out[i++] = *in;
    }
    out[i] = 0;
}

int extract_field(const char *buf, const char *field,
                  char *out, size_t outsz) {
    const char *p = strstr(buf, field);
    if (!p) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    p++;
    while (*p && (*p == ' ' || *p == '"')) ++p;
    size_t i = 0;
    while (*p && *p != '"' && *p != ',' && *p != '}' &&
           *p != '\n' && i + 1 < outsz)
        out[i++] = *p++;
    out[i] = 0;
    while (i > 0 && out[i-1] == ' ') out[--i] = 0;
    return 1;
}

void tool_list_files(int cfd, const char *params) {
    char rawpath[MAX_ARG] = ".";
    extract_field(params, "\"path\"", rawpath, sizeof(rawpath));
    char path[MAX_ARG];
    sanitize_path(rawpath, path, sizeof(path));
    if (!strlen(path)) strcpy(path, ".");
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "ls -la --color=never %s 2>&1", path);
    FILE *fp = popen(cmd, "r");
    if (!fp) { send_json(cfd, "{\"id\":null,\"type\":\"error\",\"error\":\"ls failed\"}"); return; }
    send_json(cfd, "{\"id\":null,\"type\":\"notification\",\"event\":\"tool_progress\",\"message\":\"listing files\"}");
    char output[BUFSIZE]; output[0] = 0;
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        for (char *q = line; *q; ++q)
            if (*q == '"' || *q == '\\') *q = ' ';
        strncat(output, line, sizeof(output)-strlen(output)-1);
    }
    pclose(fp);
    char out[BUFSIZE * 2];
    snprintf(out, sizeof(out),
        "{\"id\":1,\"type\":\"response\",\"result\":{\"tool\":\"list_files\",\"output\":\"%s\"}}",
        output);
    send_json(cfd, out);
}

void tool_get_time(int cfd) {
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    char buf[200];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
        tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec);
    char out[512];
    snprintf(out, sizeof(out),
        "{\"id\":1,\"type\":\"response\",\"result\":{\"tool\":\"get_time\",\"time\":\"%s\"}}",
        buf);
    send_json(cfd, out);
}

void tool_delete_older(int cfd, const char *params) {
    char rawpath[MAX_ARG] = ".";
    char days_s[32] = "0";
    extract_field(params, "\"path\"", rawpath, sizeof(rawpath));
    extract_field(params, "\"days\"", days_s, sizeof(days_s));
    char path[MAX_ARG];
    sanitize_path(rawpath, path, sizeof(path));
    int days = atoi(days_s);
    if (days <= 0) {
        send_json(cfd, "{\"id\":1,\"type\":\"response\",\"result\":{\"error\":\"invalid days value\"}}");
        return;
    }
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "find %s -maxdepth 1 -type f -mtime +%d -print -delete 2>&1", path, days);
    FILE *fp = popen(cmd, "r");
    if (!fp) { send_json(cfd, "{\"id\":null,\"type\":\"error\",\"error\":\"find failed\"}"); return; }
    char output[BUFSIZE]; output[0] = 0;
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        for (char *q = line; *q; ++q)
            if (*q == '"' || *q == '\\') *q = ' ';
        strncat(output, line, sizeof(output)-strlen(output)-1);
    }
    pclose(fp);
    char out[BUFSIZE * 2];
    snprintf(out, sizeof(out),
        "{\"id\":1,\"type\":\"response\",\"result\":{\"tool\":\"delete_older_than_days\",\"output\":\"%s\"}}",
        output);
    send_json(cfd, out);
}

int main(void) {
    int sockfd, newfd;
    struct sockaddr_in serv, cli;
    socklen_t sin_size;
    char buf[BUFSIZE];
    int yes = 1;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = INADDR_ANY;
    serv.sin_port = htons(PORT);
    memset(&(serv.sin_zero), 0, 8);
    bind(sockfd, (struct sockaddr *)&serv, sizeof(struct sockaddr));
    listen(sockfd, BACKLOG);
    printf("MCP server running on port %d\n", PORT);
    fflush(stdout);

    while (1) {
        sin_size = sizeof(struct sockaddr_in);
        newfd = accept(sockfd, (struct sockaddr *)&cli, &sin_size);
        printf("Client connected: %s\n", inet_ntoa(cli.sin_addr));
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) {
            close(sockfd);
            ssize_t nb;
            while ((nb = read(newfd, buf, BUFSIZE-1)) > 0) {
                buf[nb] = 0;
                if (strstr(buf, "\"method\":\"initialize\"") || strstr(buf, "\"method\": \"initialize\"")) {
                    send_json(newfd, "{\"id\":1,\"type\":\"response\",\"result\":{\"server\":\"LinuxCLI MCP Server\",\"version\":\"1.0\"}}");
                } else if (strstr(buf, "\"method\":\"list_tools\"") || strstr(buf, "\"method\": \"list_tools\"")) {
                    send_json(newfd,
                        "{\"id\":1,\"type\":\"response\",\"result\":{\"tools\":["
                        "{\"name\":\"list_files\",\"desc\":\"List files in a directory\",\"schema\":{\"path\":\"string\"}},"
                        "{\"name\":\"get_time\",\"desc\":\"Get server time\",\"schema\":{}},"
                        "{\"name\":\"delete_older_than_days\",\"desc\":\"Delete files older than N days\",\"schema\":{\"path\":\"string\",\"days\":\"integer\"}}]}}");
                } else if (strstr(buf, "\"method\":\"call_tool\"") || strstr(buf, "\"method\": \"call_tool\"")) {
                    if      (strstr(buf, "\"list_files\""))            tool_list_files(newfd, buf);
                    else if (strstr(buf, "\"get_time\""))              tool_get_time(newfd);
                    else if (strstr(buf, "\"delete_older_than_days\"")) tool_delete_older(newfd, buf);
                    else send_json(newfd, "{\"id\":1,\"type\":\"response\",\"result\":{\"error\":\"unknown tool\"}}");
                } else {
                    send_json(newfd, "{\"id\":null,\"type\":\"error\",\"error\":\"unknown method\"}");
                }
            }
            close(newfd);
            exit(0);
        } else if (pid > 0) {
            close(newfd);
            while (waitpid(-1, NULL, WNOHANG) > 0) {}
        } else {
            perror("fork"); close(newfd);
        }
    }
    return 0;
}
