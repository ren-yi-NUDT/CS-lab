/*
 * webserver.c —— 计算机系统实验10 Web 服务器
 *
 * 三级功能：
 *   1. 基础：监听端口、服务静态页面、fork 并发处理多请求
 *   2. webserver.ini 配置文件（root, port）
 *   3. webserver.log 访问日志（时间、IP、方法、路径）
 *
 * 参考：CSAPP 第 11.5、11.6 节（Tiny Web 服务器）
 * 编译：gcc -O2 -Wall -Werror -std=c99 -o webserver webserver.c
 * 运行：./webserver [path/to/webserver.ini]
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>      /* strcasecmp */
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* 常量 */
#define MAXLINE 8192            /* 单行最大长度 */
#define RIO_BUFSIZE 8192        /* RIO 缓冲区大小 */
#define MAXBUF  8192            /* 通用缓冲区 */

extern char **environ;

/* ============================================================
 * CSAPP RIO（Robust I/O）包，内联实现
 * ============================================================ */

typedef struct {
    int  rio_fd;                /* 与该缓冲区关联的描述符 */
    int  rio_cnt;               /* 缓冲区中未读字节数 */
    char *rio_bufptr;           /* 下一个未读字节的位置 */
    char rio_buf[RIO_BUFSIZE];  /* 内部缓冲区 */
} rio_t;

static ssize_t rio_read(rio_t *rp, char *usrbuf, size_t n)
{
    int cnt;
    while (rp->rio_cnt <= 0) {
        rp->rio_cnt = read(rp->rio_fd, rp->rio_buf, sizeof(rp->rio_buf));
        if (rp->rio_cnt < 0) {
            if (errno != EINTR) return -1;
        } else if (rp->rio_cnt == 0) {
            return 0;
        } else {
            rp->rio_bufptr = rp->rio_buf;
        }
    }
    cnt = (int)(n < (size_t)rp->rio_cnt ? n : (size_t)rp->rio_cnt);
    memcpy(usrbuf, rp->rio_bufptr, (size_t)cnt);
    rp->rio_bufptr += cnt;
    rp->rio_cnt   -= cnt;
    return (ssize_t)cnt;
}

static void rio_readinitb(rio_t *rp, int fd)
{
    rp->rio_fd     = fd;
    rp->rio_cnt    = 0;
    rp->rio_bufptr = rp->rio_buf;
}

static ssize_t rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen)
{
    size_t n;
    char   c, *bufp = usrbuf;
    for (n = 1; n < maxlen; n++) {
        ssize_t rc = rio_read(rp, &c, 1);
        if (rc == 1) {
            *bufp++ = c;
            if (c == '\n') { n++; break; }
        } else if (rc == 0) {
            if (n == 1) return 0;
            else break;
        } else {
            return -1;
        }
    }
    *bufp = 0;
    return (ssize_t)(n - 1);
}

static ssize_t rio_writen(int fd, void *usrbuf, size_t n)
{
    size_t nleft = n;
    char  *bufp  = usrbuf;
    while (nleft > 0) {
        ssize_t nwritten = write(fd, bufp, nleft);
        if (nwritten <= 0) {
            if (errno == EINTR) { nwritten = 0; continue; }
            return -1;
        }
        nleft -= (size_t)nwritten;
        bufp  += nwritten;
    }
    return (ssize_t)n;
}

/* ============================================================
 * open_listenfd —— CSAPP 风格的 socket/bind/listen 封装
 * ============================================================ */

static int open_listenfd(int port)
{
    if (port < 1 || port > 65535) {
        fprintf(stderr, "open_listenfd: invalid port %d\n", port);
        return -1;
    }

    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        perror("socket");
        return -1;
    }

    int optval = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
               (const void *)&optval, sizeof(optval));

    struct sockaddr_in serveraddr;
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family      = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port        = htons((unsigned short)port);

    if (bind(listenfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0) {
        perror("bind");
        close(listenfd);
        return -1;
    }

    if (listen(listenfd, 1024) < 0) {
        perror("listen");
        close(listenfd);
        return -1;
    }
    return listenfd;
}

/* ============================================================
 * 客户端错误响应：返回一段简短 HTML 错误页
 * ============================================================ */

static void client_error(int fd, const char *cause, int errnum,
                         const char *shortmsg, const char *longmsg)
{
    char body[MAXBUF], header[MAXBUF];

    int bn = snprintf(body, sizeof(body),
        "<html><head><title>Web Server Error</title></head>"
        "<body><h1>%d %s</h1><p>%s: %s</p>"
        "<hr><i>webserver</i>"
        "</body></html>",
        errnum, shortmsg, longmsg, cause);

    int hn = snprintf(header, sizeof(header),
        "HTTP/1.0 %d %s\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        errnum, shortmsg, bn);

    rio_writen(fd, header, (size_t)hn);
    rio_writen(fd, body,   (size_t)bn);
}

/* ============================================================
 * MIME 类型：根据扩展名返回 Content-Type
 * ============================================================ */

static const char *get_mime_type(const char *filename)
{
    const char *dot = strrchr(filename, '.');
    if (!dot) return "application/octet-stream";
    if (!strcmp(dot, ".html") || !strcmp(dot, ".htm")) return "text/html";
    if (!strcmp(dot, ".css"))                           return "text/css";
    if (!strcmp(dot, ".js"))                            return "application/javascript";
    if (!strcmp(dot, ".txt"))                           return "text/plain";
    if (!strcmp(dot, ".png"))                           return "image/png";
    if (!strcmp(dot, ".jpg") || !strcmp(dot, ".jpeg"))  return "image/jpeg";
    if (!strcmp(dot, ".gif"))                           return "image/gif";
    if (!strcmp(dot, ".ico"))                           return "image/x-icon";
    return "application/octet-stream";
}

/* ============================================================
 * 解析 URI：拼接 root + uri → filename
 *   含 ".." 返回 -1（403）；缓冲区不足返回 -2；成功返回 0
 * ============================================================ */

static int parse_uri(const char *uri, const char *root,
                     char *filename, size_t buflen)
{
    if (strstr(uri, "..") != NULL) return -1;

    char        tmp[2 * MAXLINE];
    const char *path = uri;

    if (uri[0] == '/' && uri[1] == '\0') {
        snprintf(tmp, sizeof(tmp), "%s/index.html", root);
    } else {
        if (uri[0] == '/') path = uri + 1;
        snprintf(tmp, sizeof(tmp), "%s/%s", root, path);
    }

    if (strlen(tmp) >= buflen) return -2;
    strncpy(filename, tmp, buflen);
    filename[buflen - 1] = '\0';
    return 0;
}

/* ============================================================
 * 发送静态文件
 * ============================================================ */

static void serve_static(int fd, const char *filename, int filesize)
{
    int srcfd = open(filename, O_RDONLY);
    if (srcfd < 0) {
        client_error(fd, filename, 404, "Not Found",
                     "Web server could not find this file");
        return;
    }

    char *srcbuf = malloc((size_t)filesize);
    if (!srcbuf) {
        close(srcfd);
        client_error(fd, filename, 500, "Internal Error",
                     "Memory allocation failed");
        return;
    }
    ssize_t nread = read(srcfd, srcbuf, (size_t)filesize);
    close(srcfd);
    if (nread != filesize) {
        free(srcbuf);
        client_error(fd, filename, 500, "Internal Error",
                     "Read file failed");
        return;
    }

    char header[MAXBUF];
    int  hn = snprintf(header, sizeof(header),
        "HTTP/1.0 200 OK\r\n"
        "Server: webserver\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        get_mime_type(filename), filesize);
    rio_writen(fd, header, (size_t)hn);
    rio_writen(fd, srcbuf,  (size_t)filesize);
    free(srcbuf);
}

/* ============================================================
 * 访问日志：追加一行到 ./webserver.log
 *   格式：YYYY/MM/DD HH:MM:SS IP:x.x.x.x METHOD /path
 * ============================================================ */

static void log_request(const char *logpath,
                        struct sockaddr_in *clientaddr,
                        const char *method, const char *uri)
{
    time_t    t   = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    char timestr[64];
    strftime(timestr, sizeof(timestr), "%Y/%m/%d %H:%M:%S", &tmv);

    char ip[INET_ADDRSTRLEN] = "unknown";
    if (clientaddr) {
        inet_ntop(AF_INET, &clientaddr->sin_addr, ip, sizeof(ip));
    }

    FILE *fp = fopen(logpath, "a");
    if (!fp) return;
    fprintf(fp, "%s IP:%s %s %s\n", timestr, ip, method, uri);
    fflush(fp);
    fclose(fp);
}

/* ============================================================
 * 处理一个 HTTP 连接：读请求 → 解析 → 服务 → 记日志
 * ============================================================ */

static void handle_request(int fd, const char *root,
                           struct sockaddr_in *clientaddr)
{
    rio_t rio;
    char  buf[MAXLINE];
    char  method[MAXLINE], uri[MAXLINE], version[MAXLINE];

    rio_readinitb(&rio, fd);
    if (rio_readlineb(&rio, buf, MAXLINE) <= 0) return;

    if (sscanf(buf, "%63s %63s %63s", method, uri, version) < 3) {
        client_error(fd, buf, 400, "Bad Request",
                     "Web server received a malformed request");
        return;
    }

    if (strcasecmp(method, "GET") != 0) {
        client_error(fd, method, 501, "Not Implemented",
                     "Web server does not implement this method");
        log_request("webserver.log", clientaddr, method, uri);
        return;
    }

    /* 读完剩余 header（读到空行止） */
    while (strcmp(buf, "\r\n") != 0) {
        if (rio_readlineb(&rio, buf, MAXLINE) <= 0) break;
    }

    char filename[MAXLINE];
    int  rc = parse_uri(uri, root, filename, sizeof(filename));
    if (rc == -1) {
        client_error(fd, uri, 403, "Forbidden",
                     "Path traversal is not allowed");
        log_request("webserver.log", clientaddr, method, uri);
        return;
    }
    if (rc == -2) {
        client_error(fd, uri, 414, "URI Too Long", "");
        log_request("webserver.log", clientaddr, method, uri);
        return;
    }

    struct stat st;
    if (stat(filename, &st) < 0) {
        client_error(fd, filename, 404, "Not Found",
                     "Web server could not find this file");
        log_request("webserver.log", clientaddr, method, uri);
        return;
    }
    if (!S_ISREG(st.st_mode)) {
        client_error(fd, filename, 403, "Forbidden", "Not a regular file");
        log_request("webserver.log", clientaddr, method, uri);
        return;
    }

    serve_static(fd, filename, (int)st.st_size);
    log_request("webserver.log", clientaddr, method, uri);
}

/* ============================================================
 * SIGCHLD 处理：异步回收 fork 出来的子进程
 * ============================================================ */

static void sigchld_handler(int sig)
{
    (void)sig;
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0) { /* silent reap */ }
    errno = saved_errno;
}

/* ============================================================
 * 工具：去首尾空白
 * ============================================================ */

static void trim(char *s)
{
    char *p = s;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == ' ' || s[n-1] == '\t'
                  || s[n-1] == '\r' || s[n-1] == '\n')) {
        s[--n] = 0;
    }
}

/* ============================================================
 * 解析配置文件：key=value，# 注释
 * 支持的 key：root, port。失败返回 -1，使用方用默认值
 * ============================================================ */

static int parse_config(const char *path, char *root, size_t root_len, int *port)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    char line[MAXLINE];
    while (fgets(line, sizeof(line), fp)) {
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        trim(line);
        if (line[0] == 0) continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = line;
        char *val = eq + 1;
        trim(key);
        trim(val);

        if (!strcmp(key, "root")) {
            strncpy(root, val, root_len - 1);
            root[root_len - 1] = 0;
        } else if (!strcmp(key, "port")) {
            *port = atoi(val);
        }
    }
    fclose(fp);
    return 0;
}

/* ============================================================
 * main
 * ============================================================ */

int main(int argc, char **argv)
{
    char root[MAXLINE] = ".";
    int  port          = 8080;

    const char *cfg = (argc >= 2) ? argv[1] : "webserver.ini";
    if (parse_config(cfg, root, sizeof(root), &port) == 0) {
        printf("[webserver] config loaded from %s\n", cfg);
    } else {
        printf("[webserver] config %s not found, using defaults\n", cfg);
    }
    printf("[webserver] root=%s, port=%d\n", root, port);
    fflush(stdout);

    /* 信号处理 */
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) < 0) {
        perror("sigaction(SIGCHLD)");
        exit(1);
    }
    signal(SIGPIPE, SIG_IGN);

    int listenfd = open_listenfd(port);
    if (listenfd < 0) {
        fprintf(stderr, "Failed to listen on port %d\n", port);
        exit(1);
    }
    printf("[webserver] listening on port %d\n", port);
    fflush(stdout);

    while (1) {
        struct sockaddr_in clientaddr;
        socklen_t          clientlen = sizeof(clientaddr);
        int connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen);
        if (connfd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            close(connfd);
            continue;
        }
        if (pid == 0) {
            close(listenfd);
            handle_request(connfd, root, &clientaddr);
            close(connfd);
            exit(0);
        }
        close(connfd);+
    }
    return 0;
}
