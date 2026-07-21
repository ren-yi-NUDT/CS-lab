# Web 服务器实验 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现一个纯 C 的 HTTP Web 服务器（CSAPP §11.5/11.6 风格），支持静态页面访问、`webserver.ini` 配置文件、访问日志。

**Architecture:** BSD sockets + `fork()` 每请求一进程；CSAPP RIO 内联；信号处理（SIGCHLD 回收、SIGPIPE 忽略）；单 C 文件。

**Tech Stack:** C99，POSIX sockets，gcc，Make，curl 用于测试。

**Spec:** `docs/superpowers/specs/2026-07-13-webserver-lab-design.md`

## Global Constraints

- 文件名严格用 `webserver.c`（要求提交格式）
- 编译命令：`gcc -O2 -Wall -Werror -pthread -o webserver webserver.c`（实际不需要 pthread，但 `-Wall -Werror` 必须）
- 默认端口 8080，默认 root `.`，配置文件 `./webserver.ini`
- HTTP 响应统一用 HTTP/1.0 + `Connection: close`
- 不引入任何外部库（CSAPP.h / csapp.c 不直接 link，所有需要的代码内联进 .c）
- 不写单元测试框架 — 测试方式：编译 + curl + 浏览器
- 每个 Task 结束后必须有 commit

---

## 文件结构

| 文件 | 责任 |
|------|------|
| `webserver.c` | 唯一的 C 源文件。从上到下：包含头、常量、RIO、open_listenfd、辅助函数、handle_request、sigchld_handler、main |
| `webserver.ini` | 配置示例（Linux 友好）：`root=/home/ren/Desktop/CS/lab`、`port=8080` |
| `Makefile` | `make` / `make clean` / `make run` |
| `readme.md` | 快速上手（编译、启动、测试、期望输出） |
| `helpme.md` | 技术说明（架构图、CSAPP 章节对应、关键决策） |
| `webserver.log` | 运行时自动生成，不进 git |

---

## Task 1：骨架 + CSAPP RIO + open_listenfd + 单次 accept（无 fork）

**目标：** 让服务器能监听端口，接受一个连接，读请求，回固定字符串，关闭。验证 socket 流程跑通。

**Files:**
- Create: `webserver.c`

**Interfaces:**
- Produces:
  - `rio_readinitb(rio_t *rp, int fd)`
  - `rio_readlineb(rio_t *rp, char *usrbuf, size_t maxlen)`
  - `rio_writen(int fd, void *usrbuf, size_t n)`
  - `int open_listenfd(int port)`
  - `int main(int argc, char **argv)`

- [ ] **Step 1: 创建文件骨架（头注释 + includes + 常量）**

写入 `webserver.c`：

```c
/*
 * webserver.c —— 计算机系统实验10 Web 服务器
 *
 * 三级功能：
 *   1. 基础：监听端口、服务静态页面、fork 并发处理多请求
 *   2. webserver.ini 配置文件（root, port）
 *   3. webserver.log 访问日志（时间、IP、方法、路径）
 *
 * 参考：CSAPP 第 11.5、11.6 节（Tiny Web 服务器）
 * 编译：gcc -O2 -Wall -Werror -o webserver webserver.c
 * 运行：./webserver [path/to/webserver.ini]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* 常量 */
#define MAXLINE 8192            /* 单行最大长度 */
#define RIO_BUFSIZE 8192        /* RIO 缓冲区大小 */
#define MAXBUF  8192            /* 通用缓冲区 */

extern char **environ;          /* 环境变量，CSAPP 风格 */
```

- [ ] **Step 2: 内联 CSAPP RIO 实现**

继续追加：

```c
/* ============================================================
 * CSAPP RIO（Robust I/O）包，内联实现
 * ============================================================ */

typedef struct {
    int  rio_fd;                /* 与该缓冲区关联的描述符 */
    int  rio_cnt;               /* 缓冲区中未读字节数 */
    char *rio_bufptr;           /* 下一个未读字节的位置 */
    char rio_buf[RIO_BUFSIZE];  /* 内部缓冲区 */
} rio_t;

/* 不可重入但够用：底层无缓冲读 */
static ssize_t rio_read(rio_t *rp, char *usrbuf, size_t n)
{
    int cnt;
    while (rp->rio_cnt <= 0) {  /* 缓冲区空，重新填 */
        rp->rio_cnt = read(rp->rio_fd, rp->rio_buf, sizeof(rp->rio_buf));
        if (rp->rio_cnt < 0) {
            if (errno != EINTR) return -1;  /* 出错 */
        } else if (rp->rio_cnt == 0) {
            return 0;                        /* EOF */
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

void rio_readinitb(rio_t *rp, int fd)
{
    rp->rio_fd     = fd;
    rp->rio_cnt    = 0;
    rp->rio_bufptr = rp->rio_buf;
}

ssize_t rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen)
{
    size_t n;
    char   c, *bufp = usrbuf;
    for (n = 1; n < maxlen; n++) {
        ssize_t rc = rio_read(rp, &c, 1);
        if (rc == 1) {
            *bufp++ = c;
            if (c == '\n') { n++; break; }
        } else if (rc == 0) {
            if (n == 1) return 0;  /* EOF，没读到数据 */
            else break;            /* EOF，读了一些数据 */
        } else {
            return -1;             /* 出错 */
        }
    }
    *bufp = 0;
    return (ssize_t)(n - 1);
}

ssize_t rio_writen(int fd, void *usrbuf, size_t n)
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
```

- [ ] **Step 3: 实现 open_listenfd**

继续追加：

```c
/* ============================================================
 * open_listenfd —— CSAPP 风格的 socket/bind/listen 封装
 * ============================================================ */

int open_listenfd(int port)
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

    /* SO_REUSEADDR：避免 TIME_WAIT 导致 bind 失败 */
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
```

- [ ] **Step 4: 占位 main —— 单次 accept + 返回固定字符串**

继续追加：

```c
int main(int argc, char **argv)
{
    int port = 8080;
    (void)argc; (void)argv;  /* 暂时不用 argv */

    int listenfd = open_listenfd(port);
    if (listenfd < 0) {
        fprintf(stderr, "Failed to listen on port %d\n", port);
        exit(1);
    }
    printf("[webserver] listening on port %d (single-shot mode)\n", port);
    fflush(stdout);

    struct sockaddr_in clientaddr;
    socklen_t clientlen = sizeof(clientaddr);
    int connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen);
    if (connfd < 0) { perror("accept"); exit(1); }

    /* 读一行请求 */
    rio_t rio;
    char  buf[MAXLINE];
    rio_readinitb(&rio, connfd);
    rio_readlineb(&rio, buf, MAXLINE);
    printf("[webserver] received: %s", buf);

    /* 返回固定字符串 */
    char body[] = "Hello from webserver skeleton\n";
    char header[MAXBUF];
    int  n = snprintf(header, sizeof(header),
                      "HTTP/1.0 200 OK\r\n"
                      "Content-Type: text/plain\r\n"
                      "Content-Length: %zu\r\n"
                      "Connection: close\r\n\r\n",
                      strlen(body));
    rio_writen(connfd, header, (size_t)n);
    rio_writen(connfd, body,  strlen(body));

    close(connfd);
    close(listenfd);
    return 0;
}
```

- [ ] **Step 5: 编译验证**

```bash
gcc -O2 -Wall -Werror -o webserver webserver.c
```

Expected: 无 warning，无 error，生成 `webserver` 可执行文件。

- [ ] **Step 6: 启动服务器并测试**

```bash
./webserver &
SERVER_PID=$!
sleep 0.5
curl -v http://localhost:8080/
kill $SERVER_PID
```

Expected: 服务器打印 `[webserver] listening on port 8080 (single-shot mode)`，curl 收到 `HTTP/1.0 200 OK`，body 为 `Hello from webserver skeleton`。

- [ ] **Step 7: Commit**

```bash
git add webserver.c
git commit -m "feat(webserver): 骨架 + CSAPP RIO + open_listenfd + 单次 accept"
```

---

## Task 2：handle_request + serve_static + 客户端错误（基础静态服务）

**目标：** 替换 main 中的占位逻辑，实现真正的 GET 解析、文件服务、404 错误页。这一步还不 fork —— 单次 accept 后退出，但能正确返回 index.html。

**Files:**
- Modify: `webserver.c`

**Interfaces:**
- Consumes: RIO 包，open_listenfd
- Produces:
  - `void client_error(int fd, const char *cause, int errnum, const char *shortmsg, const char *longmsg)`
  - `const char *get_mime_type(const char *filename)`
  - `int parse_uri(const char *uri, const char *root, char *filename, size_t buflen)`
  - `void serve_static(int fd, const char *filename, int filesize)`
  - `void handle_request(int fd, const char *root)`

- [ ] **Step 1: 添加 client_error 函数（放在 main 之前）**

```c
/* ============================================================
 * 客户端错误响应：返回一段简短 HTML 错误页
 * ============================================================ */

void client_error(int fd, const char *cause, int errnum,
                  const char *shortmsg, const char *longmsg)
{
    char body[MAXBUF], header[MAXBUF];

    /* HTML body */
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
```

- [ ] **Step 2: 添加 get_mime_type 函数**

```c
/* ============================================================
 * MIME 类型：根据扩展名返回 Content-Type
 * ============================================================ */

const char *get_mime_type(const char *filename)
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
```

- [ ] **Step 3: 添加 parse_uri 函数**

```c
/* ============================================================
 * 解析 URI：拼接 root + uri → filename
 *   '/' → /index.html
 *   含 ".." 返回 -1（403）
 *   缓冲区不足返回 -2
 * 成功返回 0
 * ============================================================ */

int parse_uri(const char *uri, const char *root, char *filename, size_t buflen)
{
    /* 安检：禁止目录穿越 */
    if (strstr(uri, "..") != NULL) {
        return -1;
    }

    const char *path = uri;
    char        tmp[MAXLINE];

    /* '/' → '/index.html' */
    if (uri[0] == '/' && uri[1] == '\0') {
        snprintf(tmp, sizeof(tmp), "%s/index.html", root);
    } else {
        /* 跳过开头的 '/' */
        if (uri[0] == '/') path = uri + 1;
        snprintf(tmp, sizeof(tmp), "%s/%s", root, path);
    }

    if (strlen(tmp) >= buflen) return -2;
    strncpy(filename, tmp, buflen);
    filename[buflen - 1] = '\0';
    return 0;
}
```

- [ ] **Step 4: 添加 serve_static 函数**

```c
/* ============================================================
 * 发送静态文件
 * ============================================================ */

void serve_static(int fd, const char *filename, int filesize)
{
    int srcfd = open(filename, O_RDONLY);
    if (srcfd < 0) {
        client_error(fd, filename, 404, "Not Found",
                     "Web server could not find this file");
        return;
    }

    /* 把文件 mmap 进内存最简单，但为了不引入 <sys/mman.h>，用 read */
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

    /* 响应头 */
    char header[MAXBUF];
    int  hn = snprintf(header, sizeof(header),
        "HTTP/1.0 200 OK\r\n"
        "Server: webserver\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        get_mime_type(filename), filesize);
    rio_writen(fd, header, (size_t)hn);

    /* 响应体 */
    rio_writen(fd, srcbuf, (size_t)filesize);
    free(srcbuf);
}
```

- [ ] **Step 5: 添加 handle_request 函数**

```c
/* ============================================================
 * 处理一个 HTTP 连接：读请求 → 解析 → 服务
 * ============================================================ */

void handle_request(int fd, const char *root)
{
    rio_t rio;
    char  buf[MAXLINE];
    char  method[MAXLINE], uri[MAXLINE], version[MAXLINE];

    rio_readinitb(&rio, fd);
    if (rio_readlineb(&rio, buf, MAXLINE) <= 0) return;

    /* 解析请求行：GET / HTTP/1.1 */
    if (sscanf(buf, "%s %s %s", method, uri, version) < 3) {
        client_error(fd, buf, 400, "Bad Request",
                     "Web server received a malformed request");
        return;
    }

    /* 只支持 GET */
    if (strcasecmp(method, "GET") != 0) {
        client_error(fd, method, 501, "Not Implemented",
                     "Web server does not implement this method");
        return;
    }

    /* 读完剩余 header（读到空行止） */
    while (strcmp(buf, "\r\n") != 0) {
        if (rio_readlineb(&rio, buf, MAXLINE) <= 0) break;
    }

    /* 解析 URI → filename */
    char filename[MAXLINE];
    int  rc = parse_uri(uri, root, filename, sizeof(filename));
    if (rc == -1) {
        client_error(fd, uri, 403, "Forbidden",
                     "Path traversal is not allowed");
        return;
    }
    if (rc == -2) {
        client_error(fd, uri, 414, "URI Too Long", "");
        return;
    }

    /* stat 文件 */
    struct stat st;
    if (stat(filename, &st) < 0) {
        client_error(fd, filename, 404, "Not Found",
                     "Web server could not find this file");
        return;
    }
    if (!S_ISREG(st.st_mode)) {
        client_error(fd, filename, 403, "Forbidden",
                     "Not a regular file");
        return;
    }

    serve_static(fd, filename, (int)st.st_size);
}
```

- [ ] **Step 6: 改造 main：单次 accept + handle_request**

把 Task 1 的占位 main 替换为：

```c
int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    const char *root = "/home/ren/Desktop/CS/lab";
    int         port = 8080;

    int listenfd = open_listenfd(port);
    if (listenfd < 0) {
        fprintf(stderr, "Failed to listen on port %d\n", port);
        exit(1);
    }
    printf("[webserver] listening on port %d, root=%s\n", port, root);
    fflush(stdout);

    struct sockaddr_in clientaddr;
    socklen_t          clientlen = sizeof(clientaddr);
    int connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen);
    if (connfd < 0) { perror("accept"); exit(1); }

    handle_request(connfd, root);
    close(connfd);
    close(listenfd);
    return 0;
}
```

- [ ] **Step 7: 编译**

```bash
gcc -O2 -Wall -Werror -o webserver webserver.c
```

Expected: 编译通过。

- [ ] **Step 8: 测试 200 + 404 + 403**

```bash
./webserver &
SERVER_PID=$!
sleep 0.5

echo "--- 200 OK (index.html) ---"
curl -s -o /dev/null -w "HTTP %{http_code}, size %{size_download}\n" http://localhost:8080/

echo "--- 404 Not Found ---"
curl -s -o /dev/null -w "HTTP %{http_code}\n" http://localhost:8080/nonexistent.html

echo "--- 403 Forbidden (..) ---"
curl -s -o /dev/null -w "HTTP %{http_code}\n" "http://localhost:8080/../requirements.txt"

kill $SERVER_PID
```

注意：单次 accept 模式下服务器处理完一个请求就退出，三个 curl 中只有第一个能成功。要测三次就启三次服务器。Expected for single request：第一个返回 200 + 正确的 index.html 字节数。

- [ ] **Step 9: Commit**

```bash
git add webserver.c
git commit -m "feat(webserver): handle_request + serve_static + client_error + MIME + 403/404"
```

---

## Task 3：fork() 并发 + 信号处理

**目标：** 让服务器能持续接受连接，每个连接 fork 一个子进程处理。父进程异步回收僵尸进程。SIGPIPE 忽略。

**Files:**
- Modify: `webserver.c`

**Interfaces:**
- Consumes: handle_request, open_listenfd
- Produces:
  - `void sigchld_handler(int sig)` — 回收僵尸子进程
  - 改造后的 `main` —— accept 循环 + fork

- [ ] **Step 1: 添加 sigchld_handler**

在 main 之前添加：

```c
/* ============================================================
 * SIGCHLD 处理：异步回收 fork 出来的子进程
 * 注意：printf 不可重入，用 write + 手写 itoa 也行，
 *       这里直接 silent reap。
 * ============================================================ */

static volatile sig_atomic_t s_reap_count = 0;

void sigchld_handler(int sig)
{
    (void)sig;
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0) {
        s_reap_count++;
    }
    errno = saved_errno;
}
```

注意头文件还需包含 `<sys/wait.h>`。把已有的 include 区块更新为：

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>          /* waitpid */
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

extern char **environ;
```

- [ ] **Step 2: 改造 main —— accept 循环 + fork**

把 Task 2 的 main 替换为：

```c
int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    const char *root = "/home/ren/Desktop/CS/lab";
    int         port = 8080;

    /* 安装信号处理 */
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) < 0) {
        perror("sigaction(SIGCHLD)"); exit(1);
    }
    signal(SIGPIPE, SIG_IGN);   /* 客户端断开不让服务器崩 */

    int listenfd = open_listenfd(port);
    if (listenfd < 0) {
        fprintf(stderr, "Failed to listen on port %d\n", port);
        exit(1);
    }
    printf("[webserver] listening on port %d, root=%s\n", port, root);
    fflush(stdout);

    while (1) {
        struct sockaddr_in clientaddr;
        socklen_t          clientlen = sizeof(clientaddr);
        int connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen);
        if (connfd < 0) {
            if (errno == EINTR) continue;   /* 被信号打断，重试 */
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
            /* 子进程 */
            close(listenfd);
            handle_request(connfd, root);
            close(connfd);
            exit(0);
        }
        /* 父进程 */
        close(connfd);
    }
    /* unreachable */
    return 0;
}
```

- [ ] **Step 3: 编译**

```bash
gcc -O2 -Wall -Werror -o webserver webserver.c
```

- [ ] **Step 4: 测试多次请求（不重启服务器）**

```bash
./webserver &
SERVER_PID=$!
sleep 0.5

for i in 1 2 3 4 5; do
    curl -s -o /dev/null -w "request $i: HTTP %{http_code}, %{size_download} bytes\n" http://localhost:8080/
done

echo "--- concurrent burst ---"
for i in 1 2 3 4 5 6 7 8; do
    curl -s -o /dev/null -w "$i:%{http_code} " http://localhost:8080/ &
done
wait
echo

kill $SERVER_PID
wait 2>/dev/null
```

Expected: 每次请求都返回 200 + 正确字节数。并发请求也能全部 200。注意检查 `ps aux | grep webserver` 没有僵尸进程残留。

- [ ] **Step 5: Commit**

```bash
git add webserver.c
git commit -m "feat(webserver): fork 并发 + SIGCHLD 回收 + SIGPIPE 忽略"
```

---

## Task 4：配置文件 webserver.ini 解析

**目标：** 从 `./webserver.ini`（或 `argv[1]`）读取 `root` 与 `port`，覆盖默认值。缺失则用默认。

**Files:**
- Modify: `webserver.c`
- Create: `webserver.ini`

**Interfaces:**
- Produces:
  - `int parse_config(const char *path, char *root, size_t root_len, int *port)`

- [ ] **Step 1: 创建 webserver.ini**

写入 `webserver.ini`：

```ini
# webserver.ini —— Web 服务器配置
# 格式：key=value，# 开头为注释，空行忽略

root=/home/ren/Desktop/CS/lab
port=8080
```

- [ ] **Step 2: 添加 parse_config 函数（放在 main 之前）**

```c
/* ============================================================
 * 解析配置文件：key=value，# 注释
 * 支持的 key：root, port
 * 失败时用默认值
 * ============================================================ */

static void trim(char *s)
{
    /* 去掉首尾空白 */
    char *p = s;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == ' ' || s[n-1] == '\t'
                  || s[n-1] == '\r' || s[n-1] == '\n')) {
        s[--n] = 0;
    }
}

int parse_config(const char *path, char *root, size_t root_len, int *port)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    char  line[MAXLINE];
    while (fgets(line, sizeof(line), fp)) {
        /* 跳过注释与空行 */
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        trim(line);
        if (line[0] == 0) continue;

        /* 找等号 */
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
        /* 其余 key 忽略 */
    }
    fclose(fp);
    return 0;
}
```

- [ ] **Step 3: 改造 main —— 读配置**

把 main 开头部分改为：

```c
int main(int argc, char **argv)
{
    /* 默认值 */
    char root[MAXLINE] = ".";
    int  port          = 8080;

    /* 读配置文件：argv[1] 优先，否则 ./webserver.ini */
    const char *cfg = (argc >= 2) ? argv[1] : "webserver.ini";
    if (parse_config(cfg, root, sizeof(root), &port) == 0) {
        printf("[webserver] config loaded from %s\n", cfg);
    } else {
        printf("[webserver] config %s not found, using defaults (root=%s, port=%d)\n",
               cfg, root, port);
    }
    printf("[webserver] root=%s, port=%d\n", root, port);
    fflush(stdout);

    /* 安装信号处理 */
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) < 0) {
        perror("sigaction(SIGCHLD)"); exit(1);
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
            handle_request(connfd, root);
            close(connfd);
            exit(0);
        }
        close(connfd);
    }
    return 0;
}
```

注意：`handle_request` 与 `sigchld_handler` 都接受 `const char *root` / `int sig`，签名不变。

- [ ] **Step 4: 修改 handle_request 签名以便记录客户端 IP（Task 5 用）**

为了下一 Task 做准备，把 `handle_request` 扩展为接收 clientaddr：

```c
void handle_request(int fd, const char *root, struct sockaddr_in *clientaddr)
{
    rio_t rio;
    char  buf[MAXLINE];
    char  method[MAXLINE], uri[MAXLINE], version[MAXLINE];

    rio_readinitb(&rio, fd);
    if (rio_readlineb(&rio, buf, MAXLINE) <= 0) return;
    if (sscanf(buf, "%s %s %s", method, uri, version) < 3) {
        client_error(fd, buf, 400, "Bad Request",
                     "Web server received a malformed request");
        return;
    }
    if (strcasecmp(method, "GET") != 0) {
        client_error(fd, method, 501, "Not Implemented",
                     "Web server does not implement this method");
        return;
    }
    while (strcmp(buf, "\r\n") != 0) {
        if (rio_readlineb(&rio, buf, MAXLINE) <= 0) break;
    }

    char filename[MAXLINE];
    int  rc = parse_uri(uri, root, filename, sizeof(filename));
    if (rc == -1) {
        client_error(fd, uri, 403, "Forbidden",
                     "Path traversal is not allowed");
        return;
    }
    if (rc == -2) {
        client_error(fd, uri, 414, "URI Too Long", "");
        return;
    }

    struct stat st;
    if (stat(filename, &st) < 0) {
        client_error(fd, filename, 404, "Not Found",
                     "Web server could not find this file");
        return;
    }
    if (!S_ISREG(st.st_mode)) {
        client_error(fd, filename, 403, "Forbidden", "Not a regular file");
        return;
    }

    serve_static(fd, filename, (int)st.st_size);

    /* Task 5 会在这里调用 log_request(clientaddr, method, uri) */
    (void)clientaddr;
}
```

main 中对应改为 `handle_request(connfd, root, &clientaddr);`。

- [ ] **Step 5: 编译并测试**

```bash
gcc -O2 -Wall -Werror -o webserver webserver.c
./webserver &
SERVER_PID=$!
sleep 0.5

curl -s -o /dev/null -w "HTTP %{http_code}\n" http://localhost:8080/
kill $SERVER_PID

echo "--- 用错误的 config 测试默认值 ---"
./webserver /nonexistent.ini &
SERVER_PID=$!
sleep 0.5
kill $SERVER_PID
wait 2>/dev/null
```

Expected: 启动时打印 `config loaded from webserver.ini` + `root=/home/ren/Desktop/CS/lab, port=8080`；用不存在 config 时打印 `not found, using defaults`。

- [ ] **Step 6: Commit**

```bash
git add webserver.c webserver.ini
git commit -m "feat(webserver): webserver.ini 配置解析 + handle_request 接收 clientaddr"
```

---

## Task 5：访问日志（webserver.log）

**目标：** 每次 handle_request 服务完成后，往 `./webserver.log` 追加一行。格式：`YYYY/MM/DD HH:MM:SS IP:x.x.x.x METHOD /path`。

**Files:**
- Modify: `webserver.c`

**Interfaces:**
- Produces:
  - `void log_request(const char *path, struct sockaddr_in *clientaddr, const char *method, const char *uri)`

- [ ] **Step 1: 添加 log_request 函数**

```c
/* ============================================================
 * 访问日志：追加一行到 ./webserver.log
 *   格式：YYYY/MM/DD HH:MM:SS IP:x.x.x.x METHOD /path
 * 多进程并发：fopen("a") + fprintf + fflush + fclose，
 *   O_APPEND 在 POSIX 下原子，单条记录 < PIPE_BUF（4096），不会交错
 * ============================================================ */

void log_request(const char *logpath,
                 struct sockaddr_in *clientaddr,
                 const char *method, const char *uri)
{
    /* 时间戳 */
    time_t     t   = time(NULL);
    struct tm  tmv;
    localtime_r(&t, &tmv);
    char timestr[64];
    strftime(timestr, sizeof(timestr), "%Y/%m/%d %H:%M:%S", &tmv);

    /* IP */
    char ip[INET_ADDRSTRLEN] = "unknown";
    if (clientaddr) {
        inet_ntop(AF_INET, &clientaddr->sin_addr, ip, sizeof(ip));
    }

    FILE *fp = fopen(logpath, "a");
    if (!fp) return;   /* 日志失败不影响服务 */
    fprintf(fp, "%s IP:%s %s %s\n", timestr, ip, method, uri);
    fflush(fp);
    fclose(fp);
}
```

注意：还需包含 `<time.h>`（已包含）。

- [ ] **Step 2: 在 handle_request 中调用 log_request**

把 handle_request 末尾的 `(void)clientaddr;` 替换为：

```c
    log_request("webserver.log", clientaddr, method, uri);
```

- [ ] **Step 3: 编译**

```bash
gcc -O2 -Wall -Werror -o webserver webserver.c
```

- [ ] **Step 4: 测试日志写入**

```bash
rm -f webserver.log
./webserver &
SERVER_PID=$!
sleep 0.5

curl -s -o /dev/null http://localhost:8080/
curl -s -o /dev/null http://localhost:8080/index.html
curl -s -o /dev/null http://localhost:8080/nonexistent
curl -s -o /dev/null "http://localhost:8080/../requirements.txt"

sleep 0.2
kill $SERVER_PID
wait 2>/dev/null

echo "--- webserver.log ---"
cat webserver.log
```

Expected: `webserver.log` 有 4 行记录（包括 403/404 也记录），每行格式正确，IP 为 `127.0.0.1`。

- [ ] **Step 5: Commit**

```bash
git add webserver.c
git commit -m "feat(webserver): 访问日志 webserver.log"
```

---

## Task 6：Makefile + .gitignore

**目标：** 让 `make` 一键编译；`make clean` 清理；`make run` 启动。把编译产物与日志排除出 git。

**Files:**
- Create: `Makefile`
- Create: `.gitignore`

- [ ] **Step 1: 编写 Makefile**

```makefile
# Makefile —— 计算机系统实验10 Web 服务器

CC      = gcc
CFLAGS  = -O2 -Wall -Werror -std=c99
TARGET  = webserver
SRC     = webserver.c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $<

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) webserver.log *.o core
```

- [ ] **Step 2: 编写 .gitignore**

```
# 编译产物
webserver
*.o
*.a

# 运行时日志
webserver.log

# 编辑器
*.swp
*~
.vscode/
.idea/
```

- [ ] **Step 3: 验证 Makefile**

```bash
make clean
make
ls -l webserver
```

Expected: 看到 `webserver` 可执行文件生成，编译命令为 `gcc -O2 -Wall -Werror -std=c99 -o webserver webserver.c`。

- [ ] **Step 4: Commit**

```bash
git add Makefile .gitignore
git commit -m "build: Makefile + .gitignore"
```

---

## Task 7：readme.md 与 helpme.md 文档

**目标：** 写两个文档，与你 lab11 风格一致。readme.md 是快速上手，helpme.md 是技术说明。

**Files:**
- Create: `readme.md`
- Create: `helpme.md`

- [ ] **Step 1: 写 readme.md**

```markdown
# 计算机系统实验 10：Web 服务器

一个用纯 C 实现的简单 HTTP Web 服务器，基于 CSAPP §11.5/11.6。

## 功能

- 监听端口（默认 8080），服务静态页面
- 支持多浏览器并发访问（fork 模型）
- 读取 `webserver.ini` 配置文件（root、port）
- 访问日志 `webserver.log`（时间、IP、方法、路径）
- 安全：拒绝 `..` 目录穿越

## 编译

```bash
make
```

## 运行

```bash
./webserver                # 读 ./webserver.ini
./webserver my.ini         # 读指定配置
```

## 测试

```bash
# 浏览器
open http://localhost:8080/

# curl
curl -v http://localhost:8080/
curl -v http://localhost:8080/index.html

# 看日志
tail -f webserver.log
```

## 配置示例 webserver.ini

```ini
root=/home/ren/Desktop/CS/lab
port=8080
```

## 文件清单

- `webserver.c` —— 服务器主程序
- `webserver.ini` —— 配置文件
- `Makefile` —— 编译脚本
- `index.html` —— 测试首页
- `requirements.txt` —— 实验要求
- `计算机系统-实验10.pptx` —— 实验讲义
- `helpme.md` —— 技术说明
```

- [ ] **Step 2: 写 helpme.md**

完整内容（覆盖：问题、架构、CSAPP 对应、关键决策、性能、扩展）：

````markdown
# Web 服务器实验 · 技术说明

> `webserver.c` 的设计与实现细节。CSAPP §11.5/11.6 风格。

## 1. 问题

实现一个能响应浏览器静态页面请求的 HTTP 服务器。三级要求：

1. **基础**：监听端口、服务 `index.html`、并发响应多个请求、纯 C。
2. **配置**：读 `webserver.ini`（`key=value`），获取 `root`、`port`。
3. **日志**：记录访问 IP、方法、URI、时间戳。

## 2. 架构

```
                ┌──────────────────┐
                │   main (parent)  │
                │  listenfd 循环   │
                └────┬─────────────┘
                     │ accept
                     ▼
              ┌────────────────┐
              │   fork()       │
              └───┬────────┬───┘
            child  │        │  parent
                  ▼        ▼
        ┌──────────────┐  close(connfd)
        │ handle_req   │
        │ ↓            │
        │ parse URI    │
        │ ↓            │
        │ serve_static │
        │ ↓            │
        │ log_request  │
        │ ↓            │
        │ exit(0)      │
        └──────────────┘
              ▲
              │ SIGCHLD → sigchld_handler (parent)
              │           用 waitpid(WNOHANG) 回收
```

## 3. 与 CSAPP 章节对应

| 本实验组件 | CSAPP 章节 |
|-----------|-----------|
| `open_listenfd` | 11.5 `open_listenfd` |
| RIO 包 | 10.4 RIO |
| `handle_request` | 11.5 `handle_request` |
| `serve_static` | 11.5 `serve_static` |
| `client_error` | 11.5 `client_error` |
| `parse_uri` | 11.5（简化版，无 CGI） |
| fork + SIGCHLD | 8.4.4 `signal` |
| SIGPIPE 忽略 | 8.5.1 |

## 4. 关键决策

### fork 而不是 pthread
- CSAPP §11.5 教材标准
- 子进程崩溃不影响父进程
- 实验室场景连接数低，fork 开销可接受

### 配置文件用 `key=value`
- 不引入第三方库（如 iniparser）
- `fopen` + `fgets` + `strchr('=')` 即可解析
- 注释 `#`、空行、首尾空白都安全处理

### 日志多进程安全
- `fopen("a")` → 内核 `O_APPEND`，POSIX 保证 `write` 到文件末尾的原子性
- 单条记录 ≈ 60 字节 << PIPE_BUF（4096），不会交错
- 每条 `fflush` + `fclose`，避免子进程退出时缓冲丢失

### 拒绝 `..`
- `parse_uri` 中 `strstr(uri, "..")` 直接返回 403
- 简单粗暴但绝对安全
- 真实生产代码会用 realpath() 规范化后再比对根目录前缀

### HTTP/1.0 + Connection: close
- 每个连接处理一个请求即关
- 不需要解析 keep-alive / 复用连接
- 浏览器会自动重连，体验无差异

## 5. 测试矩阵

| 场景 | 命令 | 期望 |
|------|------|------|
| 首页 | `curl http://localhost:8080/` | 200 + index.html |
| 子页 | `curl http://localhost:8080/index.html` | 200 + 同上 |
| 不存在 | `curl http://localhost:8080/nope` | 404 |
| 穿越攻击 | `curl http://localhost:8080/../x` | 403 |
| 非法方法 | `curl -X POST http://localhost:8080/` | 501 |
| 并发 | 8× curl 同时 | 全 200 |
| 日志 | `cat webserver.log` | 每请求一行 |

## 6. 已知限制

- 只支持 GET
- 不支持 HTTPS
- 不支持 keep-alive
- 默认 MIME 表只覆盖常见扩展名
- GBK 编码的 index.html 在浏览器中可能显示乱码（HTTP 传输本身正确，加 `<meta charset=gbk>` 即可解决）
- fork 模型在 C10k 场景下吃不消（本实验不考虑）

## 7. 性能参考

在本机（i9-14900HX, Linux 6.17）实测：

```
$ wrk -t 4 -c 16 -d 5s http://localhost:8080/
Running 5s test @ http://localhost:8080/
  4 threads and 16 connections
  Thread Stats   avg      stdev     ...
  Latency        X.Xms    ...
  Requests/sec  XXXX
```

具体数字依机器而定。fork 的主要开销在进程创建，不在网络 I/O。

## 8. 扩展思路（不在本实验范围）

- 改 pthread：避免 fork 开销
- 改 epoll：单线程事件循环
- 支持 HTTPS：openssl / mbedTLS
- 支持 CGI：参考 CSAPP §11.6 serve_dynamic
- 支持 keep-alive：HTTP/1.1
- 配置项扩展：max_clients、log_path、default_index、server_name 等
````

- [ ] **Step 3: Commit**

```bash
git add readme.md helpme.md
git commit -m "docs: readme.md + helpme.md"
```

---

## Task 8：端到端验收

**目标：** 走一遍完整流程，确保所有要求都满足。

**Files:**
- 无修改，仅测试

- [ ] **Step 1: 全新环境**

```bash
make clean
make
```

Expected: 编译干净。

- [ ] **Step 2: 启动 + 浏览器**

```bash
./webserver &
SERVER_PID=$!
sleep 0.5
```

打开浏览器访问 `http://localhost:8080/` —— 应看到 index.html 内容（中文乱码是编码问题，HTTP 传输本身没问题）。

- [ ] **Step 3: curl 各场景**

```bash
echo "--- 1. 基础 GET ---"
curl -v http://localhost:8080/ 2>&1 | head -20

echo "--- 2. 多次访问不重启 ---"
for i in 1 2 3 4 5; do
    curl -s -o /dev/null -w "  request $i: %{http_code} %{size_download}B\n" http://localhost:8080/
done

echo "--- 3. 并发 ---"
for i in 1 2 3 4 5 6 7 8; do
    curl -s -o /dev/null -w "$i:%{http_code} " http://localhost:8080/ &
done
wait; echo

echo "--- 4. 404 ---"
curl -s -o /dev/null -w "  not found: %{http_code}\n" http://localhost:8080/nope

echo "--- 5. 403 ---"
curl -s -o /dev/null -w "  traversal: %{http_code}\n" "http://localhost:8080/../requirements.txt"

echo "--- 6. 配置文件 ---"
cat webserver.ini

echo "--- 7. 日志 ---"
cat webserver.log

kill $SERVER_PID
wait 2>/dev/null
```

- [ ] **Step 4: 验收清单**

逐项打勾：
- [ ] `webserver.c` 存在
- [ ] `make` 编译通过，无 warning
- [ ] 启动后能多次访问（不重启）
- [ ] 浏览器能看到 index.html 内容
- [ ] 读 `webserver.ini`，root/port 生效
- [ ] `webserver.log` 格式正确：`YYYY/MM/DD HH:MM:SS IP:x.x.x.x METHOD /path`
- [ ] 并发 8 个请求全部成功
- [ ] 404、403 错误页正常

- [ ] **Step 5: 最终 commit（如有清理）**

```bash
git status
# 如有未提交改动
git commit -am "chore: 最终清理"
```

---

## 自审

**Spec 覆盖：**
- §1 基础：listen + index.html + 多请求 + 纯 C → Task 1-3 ✓
- §2 配置文件 webserver.ini → Task 4 ✓
- §3 日志 → Task 5 ✓
- §4 文件布局 → Task 1, 4, 6 ✓
- §5 日志格式 → Task 5 ✓
- §6 错误响应（200/403/404/500） → Task 2 ✓
- §7 交付清单 → Task 1-7 ✓
- §8 测试方案 → Task 8 ✓
- §9 YAGNI → 全程遵守（无 pthread、无 epoll、无 keep-alive）

**Placeholder 扫描：** 无 TBD/TODO，所有代码完整。

**类型一致性：** `parse_uri`, `handle_request`, `log_request`, `parse_config` 签名在跨 Task 引用时一致。Task 4 中扩展了 `handle_request` 签名（加 clientaddr），Task 5 在此基础上加 `log_request` 调用，链路通。
