# Web 服务器实验报告

**学号**: 202402720028
**姓名**: 任奕
**实验日期**: 2026-07-13 ～ 2026-07-21
**实验内容**: 计算机系统实验 10 —— 用纯 C 实现一个简单的 HTTP Web 服务器
**参考教材**: CSAPP 第 11.5、11.6 节（Tiny Web 服务器）

---

## 1. 实验目标

根据 `requirements.txt`，本实验分三级任务：

| 级别 | 要求 |
|------|------|
| 基础 | 实现 `webserver_学号.c`，能响应浏览器对静态页面的访问，且**不重启**可处理多次请求 |
| 配置 | 支持 `webserver.ini` 配置文件，至少包含 `root`（网站根目录）和 `port`（监听端口） |
| 日志 | 加入访问日志，记录客户端 IP、请求方法、路径、时间戳 |

提交物：`webserver_202402720028.c` 一个文件。

---

## 2. 实验环境

| 项 | 值 |
|----|----|
| 操作系统 | Linux 6.17.0-35-generic (Ubuntu) |
| CPU | Intel i9-14900HX |
| 编译器 | gcc (Ubuntu 13.x) |
| 编译选项 | `-O2 -Wall -Werror -std=c99` |
| 测试工具 | curl 8.5.0、Firefox |
| 端口 | 11451（避开 8080 占用） |

---

## 3. 总体架构

服务器采用 **CSAPP §11.5 经典的「fork-per-connection」模型**：

```
                ┌──────────────────────────┐
                │  父进程 main 循环         │
                │  (accept 阻塞等待)        │
                └──────┬───────────────────┘
                       │ accept 返回 connfd
                       │ fork()
              ┌────────┴────────┐
              ▼                 ▼
       ┌──────────────┐    ┌──────────────┐
       │  子进程       │    │  父进程       │
       │  handle_req   │    │  close(connfd)│
       │  exit(0)      │    │  loop back    │
       └──────────────┘    └──────────────┘
              ▲
              │ SIGCHLD → sigchld_handler
              │           waitpid(WNOHANG) 回收
```

**为什么 fork 而不是 pthread**：
1. CSAPP §11.5 教材标准，便于和教材对照
2. 子进程崩溃不影响父进程，鲁棒性好
3. 实验场景连接数低，fork 开销可接受

---

## 4. 任务 1：基础 HTTP 服务器

### 4.1 设计思路

一个最小可工作的 HTTP 服务器要做 5 件事：接收连接 → 读请求 → 解析 → 处理 → 写响应 → 关连接。本实验按这个顺序拆为多个小函数：

| 函数 | 行号 | 职责 |
|------|------|------|
| `open_listenfd` | 119 | socket + bind + listen |
| `rio_read` / `rio_readlineb` / `rio_writen` | 52 / 79 / 99 | RIO 包，安全读写 |
| `parse_uri` | 207 | URL → 文件路径 |
| `get_mime_type` | 187 | 文件扩展名 → Content-Type |
| `serve_static` | 232 | 读文件 + 拼响应头 + 发送 |
| `client_error` | 160 | 错误页响应 |
| `handle_request` | 301 | 主处理流程，串起上面所有函数 |
| `sigchld_handler` | 364 | 回收僵尸子进程 |

### 4.2 关键代码 1：监听 socket

```c
static int open_listenfd(int port)
{
    /* 第 1 步：创建 socket。AF_INET=IPv4，SOCK_STREAM=TCP，0=自动选协议 */
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);

    /* SO_REUSEADDR：允许重启时复用 TIME_WAIT 状态的端口，否则 bind 会失败 */
    int optval = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
               (const void *)&optval, sizeof(optval));

    /* 构造服务器地址结构 */
    struct sockaddr_in serveraddr;
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family      = AF_INET;                    /* IPv4 */
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);          /* 接收任意网卡的连接；htonl 转 4 字节为网络序 */
    serveraddr.sin_port        = htons((unsigned short)port);/* htons 转 2 字节端口为网络序 */

    /* 第 2 步：把 socket 关联到 (IP, port) */
    bind(listenfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
    /* 第 3 步：标记为被动 socket，开始等连接；1024 是内核等待队列长度 */
    listen(listenfd, 1024);
    return listenfd;
}
```

**两个关键细节**：

1. **`SO_REUSEADDR`**：服务器重启时，上次连接可能处于 TIME_WAIT，默认 `bind` 会失败（"Address already in use"）。这个选项允许复用端口。
2. **`htons(port)`**：网络字节序规定大端，但 x86 是小端。直接写 `serveraddr.sin_port = 8080` 会让对端读到错误端口号。必须用 `htons` 转换。

### 4.3 关键代码 2：RIO 包（Robust I/O）

网络读写有两个常见坑：

- **短读 (short read)**：`read(fd, buf, 100)` 不保证读满 100 字节才返回。
- **被信号打断**：`read` 阻塞时来信号，返回 -1 且 `errno=EINTR`，这不是真正的错误，应重试。

CSAPP 的 RIO 包用内部缓冲区 + 循环重试解决：

```c
static ssize_t rio_read(rio_t *rp, char *usrbuf, size_t n)
{
    int cnt;
    while (rp->rio_cnt <= 0) {                       /* 缓冲区空了，从内核补充数据 */
        rp->rio_cnt = read(rp->rio_fd, rp->rio_buf, sizeof(rp->rio_buf));
        if (rp->rio_cnt < 0) {
            if (errno != EINTR) return -1;           /* 真错返回 -1 */
            /* errno==EINTR：被信号打断，进 while 重试 */
        } else if (rp->rio_cnt == 0) {
            return 0;                                /* 对端关闭：EOF */
        } else {
            rp->rio_bufptr = rp->rio_buf;            /* 重置读指针到缓冲区开头 */
        }
    }
    /* 取 n 和缓冲区剩余字节数中较小的一个，避免读超出 */
    cnt = (int)(n < (size_t)rp->rio_cnt ? n : (size_t)rp->rio_cnt);
    memcpy(usrbuf, rp->rio_bufptr, (size_t)cnt);
    rp->rio_bufptr += cnt;                           /* 移动读指针 */
    rp->rio_cnt   -= cnt;                            /* 缓冲区剩余字节数减少 */
    return (ssize_t)cnt;
}
```

`rio_readlineb` 在 `rio_read` 之上每次读 1 字节，遇到 `\n` 就停 —— 用于按行解析 HTTP 请求。虽然看似低效，但因为内部有 8KB 缓冲区，实际只是从内存 memcpy。

### 4.4 关键代码 3：URI 解析与路径穿越防护

```c
static int parse_uri(const char *uri, const char *root,
                     char *filename, size_t buflen)
{
    if (strstr(uri, "..") != NULL) return -1;        /* 防路径穿越：URI 含 .. 直接 403 */

    char tmp[2 * MAXLINE];
    const char *path = uri;

    if (uri[0] == '/' && uri[1] == '\0') {           /* URI 是 "/"，访问首页 */
        snprintf(tmp, sizeof(tmp), "%s/index.html", root);
    } else {
        if (uri[0] == '/') path = uri + 1;           /* 去掉开头的 / 避免拼出 // */
        snprintf(tmp, sizeof(tmp), "%s/%s", root, path);
    }

    if (strlen(tmp) >= buflen) return -2;            /* 路径超长返回 414 */
    strncpy(filename, tmp, buflen);
    filename[buflen - 1] = '\0';                     /* 强制结尾，strncpy 不一定补 '\0' */
    return 0;
}
```

**安全考虑**：如果允许 `..`，客户端可发 `GET /../../../../etc/passwd HTTP/1.0`，把系统密码文件发出去（路径穿越攻击）。这里 `strstr(uri, "..")` 一刀切，简单粗暴但绝对安全。

### 4.5 关键代码 4：主处理流程

```c
static void handle_request(int fd, const char *root,
                           struct sockaddr_in *clientaddr)
{
    rio_t rio;
    char  buf[MAXLINE];
    char  method[MAXLINE], uri[MAXLINE], version[MAXLINE];

    rio_readinitb(&rio, fd);                         /* 把 RIO 缓冲区关联到连接 fd */
    if (rio_readlineb(&rio, buf, MAXLINE) <= 0) return;  /* 读 HTTP 请求行，失败就静默退出 */

    /* sscanf 拆请求行为 方法/路径/版本 三段；%63s 限长防止栈溢出 */
    if (sscanf(buf, "%63s %63s %63s", method, uri, version) < 3) {
        client_error(fd, buf, 400, "Bad Request", "...");  /* 解析失败 = 400 */
        return;
    }

    /* 只支持 GET，其他方法（POST 等）返回 501 */
    if (strcasecmp(method, "GET") != 0) {            /* strcasecmp 不区分大小写 */
        client_error(fd, method, 501, "Not Implemented", "...");
        log_request("webserver.log", clientaddr, method, uri);
        return;
    }

    /* 读完剩余 header（HTTP 协议规定头部以空行 \r\n 结束） */
    while (strcmp(buf, "\r\n") != 0) {
        if (rio_readlineb(&rio, buf, MAXLINE) <= 0) break;
    }

    /* URI → 实际文件路径 */
    char filename[MAXLINE];
    int  rc = parse_uri(uri, root, filename, sizeof(filename));
    if (rc == -1) { client_error(fd, uri, 403, "Forbidden", "Path traversal"); ...; }
    if (rc == -2) { client_error(fd, uri, 414, "URI Too Long", ""); ...; }

    /* stat 查文件信息但不打开；S_ISREG 判断是不是普通文件（不是目录/设备） */
    struct stat st;
    if (stat(filename, &st) < 0)  { client_error(fd, filename, 404, ...); ...; }  /* 文件不存在 */
    if (!S_ISREG(st.st_mode))     { client_error(fd, filename, 403, ...); ...; }  /* 不是普通文件 */

    serve_static(fd, filename, (int)st.st_size);     /* 发送文件内容 */
    log_request("webserver.log", clientaddr, method, uri);  /* 记访问日志 */
}
```

状态码覆盖矩阵：

| 场景 | 状态码 | 触发条件 |
|------|--------|---------|
| 正常 GET | 200 | 文件存在且是普通文件 |
| 请求行格式错 | 400 | `sscanf` 不到三个字段 |
| 路径穿越 | 403 | URI 含 `..` |
| 不是普通文件 | 403 | `stat` 后 `!S_ISREG` |
| 文件不存在 | 404 | `stat` 返回 -1 |
| URI 过长 | 414 | 路径超过 `MAXLINE` |
| 不支持的方法 | 501 | 非 GET |
| 内存/读文件失败 | 500 | `malloc` / `read` 失败 |

### 4.6 测试结果

并发测试（8 个 curl 同时发）：

```bash
$ for i in 1 2 3 4 5 6 7 8; do curl -s -o /dev/null \
    -w "%{http_code}\n" http://localhost:11451/ & done; wait
200
200
200
200
200
200
200
200
```

8 个请求全部 200，无僵尸进程残留（`ps aux | grep defunct` 验证）。

错误响应测试：

```bash
$ curl -o /dev/null -s -w "%{http_code}\n" http://localhost:11451/nope
404
$ curl -o /dev/null -s -w "%{http_code}\n" --path-as-is http://localhost:11451/../x
403
$ curl -o /dev/null -s -w "%{http_code}\n" -X POST http://localhost:11451/
501
```

---

## 5. 任务 2：配置文件 `webserver.ini`

### 5.1 设计思路

不引入第三方库（如 iniparser），自己用 `fopen` + `fgets` + `strchr('=')` 解析。支持：

- `key=value` 格式
- `#` 开头的注释
- 空行跳过
- 首尾空白自动 trim

### 5.2 关键代码

```c
static int parse_config(const char *path, char *root, size_t root_len, int *port)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;                              /* 配置文件不存在，调用方用默认值 */

    char line[MAXLINE];
    while (fgets(line, sizeof(line), fp)) {          /* 一行行读 */
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;                         /* 砍掉 # 后的注释 */
        trim(line);                                  /* 去掉首尾空白 */
        if (line[0] == 0) continue;                  /* 空行跳过 */

        char *eq = strchr(line, '=');                /* 找 key=value 的分隔符 */
        if (!eq) continue;                           /* 没有 = 的行跳过 */
        *eq = 0;                                     /* 把 = 替换成 '\0'，一行切成两段 */
        char *key = line;                            /* = 之前是 key */
        char *val = eq + 1;                          /* = 之后是 val */
        trim(key); trim(val);                        /* 去首尾空白 */

        if (!strcmp(key, "root")) {                  /* 处理 root 字段 */
            strncpy(root, val, root_len - 1);
            root[root_len - 1] = 0;                  /* 强制结尾 '\0' */
        } else if (!strcmp(key, "port")) {           /* 处理 port 字段 */
            *port = atoi(val);                       /* atoi：字符串 → int */
        }
    }
    fclose(fp);
    return 0;
}
```

### 5.3 配置文件示例

`webserver.ini`：

```ini
# key=value 格式，# 开头为注释

root=/home/ren/Desktop/CS/lab
port=8080
```

`main` 函数中的调用：

```c
const char *cfg = (argc >= 2) ? argv[1] : "webserver.ini";
if (parse_config(cfg, root, sizeof(root), &port) == 0) {
    printf("[webserver] config loaded from %s\n", cfg);
} else {
    printf("[webserver] config %s not found, using defaults\n", cfg);
}
```

命令行可显式指定配置文件，不指定则默认读 `./webserver.ini`。

### 5.4 测试结果

```bash
$ ./webserver webserver.ini
[webserver] config loaded from webserver.ini
[webserver] root=/home/ren/Desktop/CS/lab, port=8080
[webserver] listening on port 8080
```

修改 `port=11451` 后无需重新编译，重启服务器即生效。

---

## 6. 任务 3：访问日志 `webserver.log`

### 6.1 设计思路

格式严格按实验要求：

```
YYYY/MM/DD HH:MM:SS IP:x.x.x.x METHOD /path
```

示例（实际日志文件 `webserver.log` 内容）：

```
2026/07/13 21:25:54 IP:127.0.0.1 GET /
2026/07/13 21:27:00 IP:127.0.0.1 GET /nope
2026/07/13 21:27:00 IP:127.0.0.1 GET /../requirements.txt
2026/07/21 12:39:33 IP:127.0.0.1 GET /
2026/07/21 12:39:33 IP:127.0.0.1 GET /favicon.ico
```

### 6.2 关键代码

```c
static void log_request(const char *logpath,
                        struct sockaddr_in *clientaddr,
                        const char *method, const char *uri)
{
    /* 获取当前时间，格式化为 YYYY/MM/DD HH:MM:SS */
    time_t    t   = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);                           /* localtime_r：线程安全版（_r = reentrant） */
    char timestr[64];
    strftime(timestr, sizeof(timestr), "%Y/%m/%d %H:%M:%S", &tmv);

    /* 把 sockaddr_in 里的 IP 二进制转成可读字符串 "127.0.0.1" */
    char ip[INET_ADDRSTRLEN] = "unknown";
    if (clientaddr) {
        inet_ntop(AF_INET, &clientaddr->sin_addr, ip, sizeof(ip));
    }

    /* "a" 模式 = append；内核 O_APPEND 保证多进程写不会交错 */
    FILE *fp = fopen(logpath, "a");
    if (!fp) return;                                 /* 打不开就静默放弃，不能影响主流程 */
    fprintf(fp, "%s IP:%s %s %s\n", timestr, ip, method, uri);
    fflush(fp);                                      /* 强制刷盘，避免子进程 exit 时丢缓冲 */
    fclose(fp);
}
```

### 6.3 多进程安全性

由于采用 fork 并发，多个子进程可能同时写日志。这里的安全性靠三点保证：

1. **`fopen("a")`** 对应内核 `O_APPEND`，POSIX 保证 `write` 原子追加到文件末尾
2. **单条记录 ≈ 60 字节 << PIPE_BUF（4096）**，单次 `write` 不会与其他进程交错
3. **`fflush` + `fclose`** 确保数据落盘，避免子进程 `exit(0)` 时丢失缓冲区内容

实测 8 个并发请求，日志文件每条记录完整无交错。

### 6.4 调用位置

在 `handle_request` 的每个出口（成功、404、403、501、414）都调用一次 `log_request`，确保所有请求（不论成功失败）都被记录。

---

## 7. 信号处理

### 7.1 SIGCHLD 回收僵尸进程

子进程退出后变僵尸，等待父进程收尸。父进程不能阻塞 `wait`（还要去 `accept`），所以用信号异步回收：

```c
static void sigchld_handler(int sig)
{
    (void)sig;                                       /* 消除「未使用参数」警告 */
    int saved_errno = errno;                         /* 保存 errno，避免信号处理函数污染主程序 */
    while (waitpid(-1, NULL, WNOHANG) > 0) {         /* 循环收尸所有已死子进程；-1=任意；WNOHANG=非阻塞 */
        /* silent reap */
    }
    errno = saved_errno;                             /* 恢复 errno */
}
```

要点：
- `waitpid(-1, ..., WNOHANG)` 非阻塞，没有也不卡
- `while` 循环而非 `if`，因为可能有多个子进程同时死了
- `saved_errno` 防止信号处理函数破坏主程序的 `errno`

### 7.2 main 中的注册

```c
struct sigaction sa;
sa.sa_handler = sigchld_handler;
sigemptyset(&sa.sa_mask);                            /* 处理 SIGCHLD 期间不额外屏蔽其他信号 */
sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;             /* SA_RESTART：被信号打断的阻塞调用自动重启 */
                                                    /* SA_NOCLDSTOP：子进程暂停（SIGSTOP）不发 SIGCHLD */
sigaction(SIGCHLD, &sa, NULL);
signal(SIGPIPE, SIG_IGN);                            /* 忽略 SIGPIPE：客户端断开后 write 触发，默认会杀进程 */
```

- `SA_RESTART`：被信号打断的阻塞系统调用（如 `accept`）自动重启，避免莫名其妙返回 `EINTR`
- `SA_NOCLDSTOP`：子进程暂停（SIGSTOP）不发 SIGCHLD，只在真正退出时发
- `SIGPIPE` 忽略：客户端突然断开后 `write` 触发 SIGPIPE，默认会杀进程，必须忽略

---

## 8. 测试矩阵

| 场景 | 命令 | 期望 | 实测 |
|------|------|------|------|
| 首页 | `curl http://localhost:11451/` | 200 + index.html | ✅ 200 |
| 子页 | `curl http://localhost:11451/index.html` | 200 | ✅ 200 |
| 不存在 | `curl http://localhost:11451/nope` | 404 | ✅ 404 |
| 路径穿越 | `curl --path-as-is http://localhost:11451/../x` | 403 | ✅ 403 |
| 非法方法 | `curl -X POST http://localhost:11451/` | 501 | ✅ 501 |
| 8 并发 | 8 × curl 同时 | 全 200 | ✅ 全 200 |
| 大文件 | 77 KB 的 index.html | 200 + 完整传输 | ✅ 完整 |
| 日志 | `cat webserver.log` | 每请求一行 | ✅ 完整 |
| 不重启 | 第 2 个请求 | 仍能服务 | ✅ |

---

## 9. 关键决策与权衡

### 9.1 fork 而非 pthread

**选择**：fork
**理由**：
- CSAPP §11.5 标准做法，与教材对齐
- 子进程崩溃不影响父进程，鲁棒性强
- 不需要锁，编程模型简单

**代价**：
- 每连接 fork 一次，开销大（约 1ms 量级）
- C10k 场景下吃不消
- 不在本实验考虑范围

### 9.2 HTTP/1.0 + Connection: close

**选择**：HTTP/1.0，每个连接处理一个请求即关
**理由**：
- 不需要解析 keep-alive、不需要复用连接
- 浏览器会自动重连，体验无差异
- 代码量减少约 30%

### 9.3 路径穿越用 strstr 而非 realpath

**选择**：`strstr(uri, "..")` 直接返回 403
**理由**：
- 简单、绝对安全
- 误伤（合法路径含 `..`）的情况极罕见

**代价**：
- 不优雅，生产代码会用 `realpath()` 规范化后检查是否在 root 下

### 9.4 配置文件不引入第三方库

**选择**：自己用 `fgets` + `strchr` 解析
**理由**：
- key=value 格式极简单，几十行搞定
- 不引入依赖，编译干净

---

## 10. 安全性分析

对自实现的 webserver 进行安全审视：

| 攻击面 | 是否可利用 | 严重度 | 说明 |
|--------|-----------|--------|------|
| 路径穿越 | ❌ | 低 | `strstr(uri, "..")` 拦死 |
| 缓冲区溢出 | ❌ | 低 | 所有读写都用 `MAXLINE` 边界检查，`sscanf` 用 `%63s` 限长 |
| 日志注入 | ❌ | 低 | `sscanf %s` 天然过滤换行符 |
| 信息泄露（路径回显） | ✅ | 中 | 404 错误页含服务器内部绝对路径，便于攻击者了解布局 |
| Header 指纹 | ✅ | 低 | `Server: webserver_202402720028` 暴露身份 |
| DoS（fork 失控） | ✅ | 高 | 无并发上限、无超时，易被慢连接攻击 |

**主要风险是 DoS**。这是所有 fork-per-connection 模型的通病。生产环境的解决方法是 epoll + 线程池（nginx 做法），不在本实验范围。

---

## 11. 文件清单

```
lab/
├── webserver_202402720028.c   # 提交文件（487 行）
├── webserver.ini              # 默认配置
├── www.ini                    # 测试配置（端口 11451）
├── Makefile                   # 编译脚本
├── run.sh                     # 一键启动脚本
├── index.html                 # 测试首页（NUDT 本科招生网）
├── index-yjs.html             # 测试页（NUDT 研究生招生网）
├── webserver.log              # 访问日志（实测）
├── requirements.txt           # 实验要求
├── helpme.md                  # 小白向技术详解
├── readme.md                  # 快速上手
└── report.md                  # 本报告
```

---

## 12. 总结与反思

### 12.1 完成情况

| 任务 | 状态 |
|------|------|
| 任务 1：基础 HTTP 服务器 | ✅ 完成，并发测试通过 |
| 任务 2：配置文件 | ✅ 完成，支持命令行指定 |
| 任务 3：访问日志 | ✅ 完成，多进程安全 |

### 12.2 学到的东西

1. **HTTP 协议本质是文本协议**：请求行 + 头部 + 空行 + body，没什么神秘的
2. **socket 编程四件套**：socket / bind / listen / accept，理解了服务器「等连接」的本质
3. **fork 并发模型**：理解了僵尸进程、SIGCHLD、SA_RESTART 这些概念
4. **字节序问题**：`htons` / `htonl` 不是装饰，写错就监听错端口
5. **多进程文件写入安全**：`O_APPEND` 的原子性 + `PIPE_BUF` 保证

### 12.3 不足与改进方向

1. **不支持 keep-alive**：每个请求都重新握手，效率低
2. **不支持 HTTPS**：明文传输，无法生产使用
3. **不支持 CGI / 动态内容**：只能服务静态文件
4. **fork 开销大**：高并发场景应改 epoll + 线程池
5. **无超时机制**：慢连接攻击会让子进程卡死
6. **错误页泄露内部路径**：应该改成只回显 URI

### 12.4 与 CSAPP 教材的差异

本实现基本忠实于 CSAPP §11.5 的 Tiny 服务器，主要差异：

| 教材 | 本实现 | 原因 |
|------|--------|------|
| 无配置文件 | 加了 `webserver.ini` | 任务 2 要求 |
| 无日志 | 加了 `webserver.log` | 任务 3 要求 |
| 用 `rio_writen` 直接写文件 | 同 | 一致 |
| 支持 CGI（serve_dynamic） | 不支持 | 任务未要求，且 CGI 已过时 |

---

## 附录 A：编译与运行

```bash
# 编译
make                                    # 或 gcc -O2 -Wall -Werror -std=c99 \
                                         #     -o webserver webserver_202402720028.c

# 启动（默认读 ./webserver.ini）
./webserver

# 用指定配置启动
./webserver www.ini

# 一键脚本（推荐）
./run.sh www.ini

# 测试
curl -v http://localhost:11451/
tail -f webserver.log
```

## 附录 B：实际启动日志

```
[run] 启动 webserver，配置文件：www.ini
[run] Ctrl-C 停止

[webserver] config loaded from www.ini
[webserver] root=/home/ren/Desktop/CS/lab, port=11451
[webserver] listening on port 11451

================================================
  Web 服务器已启动，PID=12345
  访问地址：http://localhost:11451/
  日志文件：/home/ren/Desktop/CS/lab/webserver.log
  停止方式：Ctrl-C
================================================
```

## 附录 C：实际 curl 响应

```bash
$ curl -v http://localhost:11451/
*   Trying 127.0.0.1:11451...
* Connected to localhost (127.0.0.1) port 11451
> GET / HTTP/1.1
> Host: localhost:11451
> User-Agent: curl/8.5.0
> Accept: */*
>
< HTTP/1.0 200 OK
< Server: webserver_202402720028
< Content-Type: text/html
< Content-Length: 77704
< Connection: close
<
{ [77704 bytes data]
* Closing connection
```

完整 HTTP 请求行、头部、空行、body 都按协议规范发送和接收，状态码 200。
