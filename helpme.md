# Web 服务器实验 · 小白向详解

> 这份文档假设你**完全不懂网络编程**，但 C 语言指针、struct、文件 I/O 是 OK 的。
> 从「浏览器输入 URL 发生了什么」一路讲到代码每一行为什么那么写。
> 读完后你应该能回答：HTTP 协议长什么样、socket 怎么用、这个 `.c` 文件每一块在干嘛。

---

## 第 0 章 · 你能从这份文档得到什么

`webserver.c` 是一个用 487 行 C 写出来的、能真正给浏览器提供网页的小服务器。但它读起来不直观：里面塞了 HTTP 协议、socket 编程、fork 并发、信号处理、文件 I/O、配置解析一堆东西，而且风格是 CSAPP 教材那种「为了教学清晰所以刻意简化」的写法。

这份文档的目标只有一个：**让一个完全没接触过网络编程的人，看完后能逐行讲清楚这个文件每一段在干什么、为什么这么写**。

阅读顺序建议**从头到尾**，因为后面章节的概念都建立在前面之上。如果你卡在某一章，不要跳过去，回去重看。

---

## 第 1 章 · 背景：浏览器和服务器在干什么

### 1.1 一个最常见的场景

你打开浏览器，在地址栏输入 `http://localhost:8080/`，按下回车。屏幕上出现一个网页。

中间发生了什么？把这件事拆成最简单的两个角色：

```
   ┌──────────┐         ┌──────────┐
   │  浏览器   │ ────→   │  服务器   │
   │ (client) │ ←────   │ (server) │
   └──────────┘         └──────────┘
```

- **浏览器**叫**客户端 (client)**：它主动发起请求，说「我要某个文件」。
- **服务器 (server)**：它一直在某个端口上**等**，谁来找它要文件，它就把文件发过去。

这个过程像打电话：

| 打电话 | 浏览器 ↔ 服务器 |
|--------|----------------|
| 你拨号 | 浏览器发起连接 |
| 对方接听 | 服务器 `accept` |
| 你说「找一下李雷」 | 浏览器发 `GET /index.html` |
| 对方说「在的，电话递给他」 | 服务器回 `200 OK` + 文件内容 |
| 双方挂电话 | 双方 `close` 连接 |

关键点：**客户端和服务器之间只是互相发送文本**。这些文本必须双方都看得懂，所以得有个约定 —— 这个约定就是下一章要讲的 HTTP。

### 1.2 「服务器一直在等」是什么意思

打开浏览器之前，服务器程序已经在跑了。它在做什么？它在执行一个**死循环**：

```c
while (1) {
    等电话铃响();      // accept，没人来就阻塞
    接听();
    跟对方对话();
    挂断();
}
```

只要你不按 Ctrl-C，它就永远在等下一个请求。这就是「服务器」三个字的字面意思 —— **服务的人**。

### 1.3 一台机器怎么同时是客户端和服务器

实验时你会看到 `http://localhost:8080/`。`localhost` 是「本机」的意思。也就是说，**你的电脑既是浏览器（客户端），又是服务器**。

- 你的浏览器通过 8080 这个「门牌号」找服务器
- 服务器程序在你的电脑上监听 8080 这个门牌号
- 它们通过这个门牌号互相收发数据

「门牌号」的学名叫**端口 (port)**。下一节细讲。

---

## 第 2 章 · HTTP：它们用什么语言对话

### 2.1 HTTP 请求长什么样

HTTP 是 **H**yper**T**ext **T**ransfer **P**rotocol（超文本传输协议）的缩写。说白了就是**客户端和服务器互相发文本时遵守的格式**。

我们可以用 `curl -v` 看一个真实的请求。在终端跑：

```bash
curl -v http://localhost:8080/
```

你会看到（节选）：

```
> GET / HTTP/1.1          ← 请求行
> Host: localhost:8080    ← 头部
> User-Agent: curl/7.81.0
> Accept: */*
>                         ← 空行，标志头部结束
```

第一行叫**请求行**，三个字段用空格分开：

```
GET       /         HTTP/1.1
  │       │            │
方法    路径        协议版本
```

- **方法 (method)**：最常见的就是 `GET`，意思是「我要读这个文件」。还有 `POST`（提交表单）、`PUT`、`DELETE` 等，本实验只支持 `GET`。
- **路径 (URI)**：浏览器想要服务器上的哪个文件。`/` 表示首页。
- **协议版本**：基本固定是 `HTTP/1.1` 或 `HTTP/1.0`。

后面跟着若干**头部 (header)**，每行一个 `名字: 值`。本实验其实**不需要**用这些头部，但必须把它们读掉（不能卡在那），否则就读不到请求的末尾。

最后是一个**空行**（`\r\n\r\n`），表示头部结束。这是 HTTP 协议的硬性规定，浏览器必须发，服务器必须靠它判断「头部读完了」。

### 2.2 HTTP 响应长什么样

服务器回复的格式也类似：

```
HTTP/1.0 200 OK                  ← 状态行
Server: webserver   ← 头部
Content-Type: text/html
Content-Length: 272
Connection: close
                                 ← 空行
<html>...实际网页内容...</html>   ← body
```

第一行叫**状态行**，三个字段：

```
HTTP/1.0   200     OK
   │       │        │
 协议版本  状态码   状态说明
```

**状态码 (status code)** 是个数字，告诉客户端「我这边怎么样」：

| 码 | 含义 | 你会见到的场景 |
|----|------|--------------|
| 200 | OK | 一切正常，文件给你 |
| 400 | Bad Request | 客户端发的请求格式不对 |
| 403 | Forbidden | 文件存在但不让你访问（比如 `..` 路径穿越） |
| 404 | Not Found | 文件不存在 |
| 501 | Not Implemented | 方法服务器不支持（比如 POST） |
| 500 | Internal Error | 服务器自己出了问题 |

头部之后是一个**空行**，然后是真正的**响应体 (body)** —— 也就是网页本身的 HTML。

### 2.3 一个最小可工作的 HTTP 服务器要做什么

把上面两节合起来，一个 HTTP 服务器要做的事情非常简单：

1. **接收连接**：客户端来一个连接，服务器接下它。
2. **读请求**：从连接里读文本，解析出**方法**和**路径**。
3. **处理**：根据路径找到文件，读出来。
4. **写响应**：先写状态行 + 头部 + 空行，再写文件内容。
5. **关连接**：完成。

整个 `webserver.c` 487 行代码，本质上就是在做这 5 件事，加一些「让多个客户端能同时连」、「读配置文件」、「写日志」的辅助功能。

---

## 第 3 章 · socket：C 程序怎么联网

### 3.1 文件描述符是什么

在 Unix/Linux 里，**一切皆文件**。打开一个真文件得到一个整数（文件描述符，fd），之后 `read(fd, ...)`、`write(fd, ...)` 就能读写。

网络连接也一样。当你和远端建立一条连接，内核给你一个整数 fd，之后 `read`/`write` 这个 fd 就是在收发网络数据。**对程序员来说，读写网络和读写文件几乎没区别**。

### 3.2 服务器端 socket 的四步

服务器要让客户端能连上，需要四步：

```
socket()  →  bind()  →  listen()  →  accept()
```

用「开窗口收信」类比：

| 系统调用 | 类比 | 干嘛 |
|---------|------|------|
| `socket()` | 买一个空信箱 | 创建一个网络端点，返回 fd |
| `bind()` | 把信箱钉在某门牌号 | 把 fd 关联到某个 IP + 端口 |
| `listen()` | 在信箱上贴「可投递」 | 标记这个 fd 是被动套接字，可以接连接 |
| `accept()` | 等邮递员来送信 | 阻塞等连接，来一个就返回一个**新** fd |

注意 `accept` 返回的是一个**新的 fd**！原来的 listening fd 继续等下一个连接，新 fd 用来跟刚刚连上来的客户端对话。这是初学者最容易迷糊的点。

### 3.3 端口和 IP

每台机器有 2^16 = 65536 个端口。一台机器上可能同时跑很多服务器（web、ssh、数据库），靠端口区分：

- 80 是 HTTP 默认端口（需要 root 权限）
- 22 是 SSH
- 8080 是 HTTP 的「备用」端口（不需要 root），本实验就用这个

IP 是机器的地址，端口是机器上某个程序的「门牌号」。两条信息合起来 `(IP, port)` 才能定位到「这台机器上的这个程序」。

### 3.4 socket 编程的最小骨架

```c
int listenfd = socket(AF_INET, SOCK_STREAM, 0);     // 买信箱

struct sockaddr_in addr;
addr.sin_family      = AF_INET;                     // IPv4
addr.sin_addr.s_addr = htonl(INADDR_ANY);           // 任意网卡都接受
addr.sin_port        = htons(8080);                 // 端口 8080

bind(listenfd, (struct sockaddr *)&addr, sizeof(addr));  // 钉门牌号
listen(listenfd, 1024);                             // 贴「可投递」

while (1) {
    int connfd = accept(listenfd, NULL, NULL);      // 等信
    // ... 用 connfd 跟客户端对话 ...
    close(connfd);
}
```

`AF_INET` 表示 IPv4，`SOCK_STREAM` 表示 TCP（可靠传输）。这两个参数本实验固定这么写，先不用纠结。

`htonl` / `htons` 是「主机字节序 → 网络字节序」的转换。网络规定数字按大端传输，但你的 CPU 可能是小端，所以必须转换。**记住端口号要套 `htons`，IP 要套 `htonl`** 就行。

完整封装在代码的 `open_listenfd` 函数里，第 5.1 节会逐行讲。

---

## 第 4 章 · 整个程序的骨架

### 4.1 main 函数在干什么

打开 `webserver.c`，跳到最后看 `main` 函数（第 428 行）。剥掉细节，骨架是这样：

```c
int main(int argc, char **argv)
{
    char root[MAXLINE] = ".";
    int  port          = 8080;

    parse_config("webserver.ini", root, ..., &port);   // 读配置

    /* 注册 SIGCHLD 信号处理（见 4.4） */
    signal(SIGCHLD, sigchld_handler);
    signal(SIGPIPE, SIG_IGN);                          // 见 4.5

    int listenfd = open_listenfd(port);                // socket+bind+listen

    while (1) {                                        // 死循环
        int connfd = accept(listenfd, ...);            // 等连接

        pid_t pid = fork();                            // 复制自己
        if (pid == 0) {                                // 子进程
            close(listenfd);                           // 子进程不需要监听 fd
            handle_request(connfd, root, ...);         // 处理这个请求
            close(connfd);
            exit(0);                                   // 子进程退出
        }
        close(connfd);                                 // 父进程关掉 connfd
    }
}
```

整个生命周期：

```
                ┌──────────────────────────┐
                │  父进程 main 循环         │
                │  (永远在 accept)          │
                └──────┬───────────────────┘
                       │ accept 返回 connfd
                       │ fork()
              ┌────────┴────────┐
              ▼                 ▼
       ┌──────────┐         ┌──────────┐
       │ 子进程 1  │         │ 父进程    │
       │ handle    │         │ close     │
       │ _request  │         │ (connfd)  │
       │ exit(0)   │         │ loop back │
       └──────────┘         └──────────┘
```

为什么 `fork` 而不是直接 `handle_request`？因为 `handle_request` 要读文件、写网络、可能很慢（一个浏览器请求几百毫秒），如果父进程亲自处理，期间来了别的客户端就只能排队。**fork 一个子进程专门处理这一个连接，父进程立刻回去 accept 下一个**，这就实现了并发。

### 4.2 fork 是什么

`fork()` 是 Unix 系统调用，**把当前进程复制一份**。返回值：

- 在**父进程**里返回子进程的 PID（>0）
- 在**子进程**里返回 0
- 失败返回 -1

这是个很奇怪的 API：同一个 `fork()` 调用，**返回两次**，一次在父进程，一次在子进程。代码靠返回值区分自己是在哪个进程里：

```c
pid_t pid = fork();
if (pid == 0) {
    // 这里只有子进程会进来
} else {
    // 这里只有父进程会进来
}
```

子进程是父进程的**完整副本**：变量值一样、文件描述符一样、内存一样。所以子进程一开始也能看到 `connfd`，可以直接用它跟客户端对话。但子进程**不需要** `listenfd`（它不负责接新连接），所以 `close(listenfd)` 释放掉。父进程反过来 —— 它**不需要 connfd**（已经交给子进程了），所以也 `close(connfd)`。

### 4.3 僵尸进程是什么

子进程退出后并不会立刻消失 —— 它会变成「僵尸进程 (zombie)」，保留一条记录（PID、退出状态等），等父进程来「收尸」。如果父进程从不收尸，僵尸就一直堆着，最终占满进程表。

收尸的系统调用是 `wait` / `waitpid`。但你不能让父进程在 main 循环里阻塞等 —— 它还得去 `accept`。怎么办？

### 4.4 SIGCHLD 信号

Unix 的解决方案：**子进程退出时，内核给父进程发一个 SIGCHLD 信号**。父进程可以预先注册一个信号处理函数，这个函数会在信号到来时被异步调用。在函数里用 `waitpid` 收尸就行。

```c
static void sigchld_handler(int sig)
{
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0) { /* 收掉所有已死的子进程 */ }
    errno = saved_errno;
}
```

几个细节：

- `waitpid(-1, ..., WNOHANG)`：`-1` 表示等任意子进程，`WNOHANG` 表示没有就立刻返回不阻塞。
- 用 `while` 循环而不是 `if`，因为可能有多个子进程同时死了，要一次性收完。
- `saved_errno` 是因为信号处理函数可能打断正在修改 `errno` 的代码，要保存恢复。
- `SA_RESTART` 标志（在 main 中 `sigaction` 设置）：让被信号打断的阻塞系统调用自动重启，不然 `accept` 可能莫名其妙返回 EINTR 错误。

### 4.5 SIGPIPE 为什么要忽略

如果客户端突然断开连接，而服务器还在 `write`，内核会发 SIGPIPE 信号，**默认行为是杀掉进程**。这对服务器是灾难 —— 一个客户端的破连接不能把整个服务器搞崩。所以 `signal(SIGPIPE, SIG_IGN)` 把这个信号忽略掉，`write` 会返回 -1，正常处理错误就行。

---

## 第 5 章 · 逐函数剖析代码

这一章是核心。每个函数都会按 **它是干嘛的 / 一个最小例子 / 代码逐行讲 / 为什么这么写** 四段来写。

### 5.1 `open_listenfd`（监听端口）

**位置**：`webserver.c:119`

**干嘛的**：把第 3.2 节的 socket+bind+listen 三步打包成一个函数，返回一个监听 fd。

**代码逐行**：

```c
static int open_listenfd(int port)
{
    if (port < 1 || port > 65535) {            // 防御性检查
        fprintf(stderr, "invalid port %d\n", port);
        return -1;
    }

    int listenfd = socket(AF_INET, SOCK_STREAM, 0);   // 第 1 步：买信箱
    if (listenfd < 0) { perror("socket"); return -1; }

    int optval = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
               (const void *)&optval, sizeof(optval));  // 见下方"为什么"

    struct sockaddr_in serveraddr;
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family      = AF_INET;              // IPv4
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);    // 任意网卡
    serveraddr.sin_port        = htons((unsigned short)port);

    if (bind(listenfd, (struct sockaddr *)&serveraddr,
             sizeof(serveraddr)) < 0) {                // 第 2 步：钉门牌
        perror("bind");
        close(listenfd);
        return -1;
    }

    if (listen(listenfd, 1024) < 0) {                  // 第 3 步：贴可投递
        perror("listen");
        close(listenfd);
        return -1;
    }
    return listenfd;
}
```

**为什么 `SO_REUSEADDR`**：服务器重启时，上次的连接可能还没完全关闭，端口处于 TIME_WAIT 状态。默认情况下 `bind` 会失败（"Address already in use"）。这个选项告诉内核「这个端口我复用，别卡我」。**所有服务器代码都该加这行**。

**为什么 `INADDR_ANY`**：一台机器可能有多个网卡（有线、WiFi、lo），`INADDR_ANY` 让服务器接收任意网卡来的连接。如果指定具体 IP，就只能从那个网卡连。

### 5.2 RIO 包（安全的读写）

**位置**：`webserver.c:45-113`

**干嘛的**：CSAPP 教材自己实现的一套「Robust I/O」函数，封装了 `read`/`write`，处理了两个坑：

1. **短读 (short read)**：`read(fd, buf, 100)` 不保证读满 100 字节才返回。它可能只读 4 字节就回来（比如内核缓冲区暂时只有这么多）。如果你以为读满了，就会出错。
2. **被信号打断**：`read` 正在阻塞时，如果来了一个信号，`read` 会返回 -1 且 `errno=EINTR`。这不是真正的错误，应该重试。

RIO 提供三个主要函数：

- `rio_readinitb(rp, fd)`：把一个 `rio_t` 结构关联到 fd。
- `rio_readlineb(rp, buf, maxlen)`：从 fd 读**一行**（读到 `\n` 为止）。HTTP 是文本协议按行解析，这个超有用。
- `rio_writen(fd, buf, n)`：保证写满 `n` 字节才返回。

`rio_t` 结构（第 45 行）：

```c
typedef struct {
    int  rio_fd;                // 关联的描述符
    int  rio_cnt;               // 缓冲区里还有多少字节没读
    char *rio_bufptr;           // 下一个该读的字节
    char rio_buf[RIO_BUFSIZE];  // 内部缓冲区
} rio_t;
```

**为什么要内部缓冲区**：每次 `read` 都陷进内核是有开销的。RIO 一次从内核拿一大块（8192 字节）放进 `rio_buf`，之后 `rio_readlineb` 一字节一字节读的时候，就直接从内存拿，不用陷进内核。

`rio_read`（第 52 行）是底层引擎：

```c
static ssize_t rio_read(rio_t *rp, char *usrbuf, size_t n)
{
    int cnt;
    while (rp->rio_cnt <= 0) {                          // 缓冲区空了
        rp->rio_cnt = read(rp->rio_fd, rp->rio_buf, sizeof(rp->rio_buf));
        if (rp->rio_cnt < 0) {
            if (errno != EINTR) return -1;              // 真错就返回-1
            // EINTR：被信号打断，进 while 重试
        } else if (rp->rio_cnt == 0) {
            return 0;                                   // EOF
        } else {
            rp->rio_bufptr = rp->rio_buf;               // 重置指针
        }
    }
    cnt = (int)(n < (size_t)rp->rio_cnt ? n : (size_t)rp->rio_cnt);
    memcpy(usrbuf, rp->rio_bufptr, (size_t)cnt);
    rp->rio_bufptr += cnt;
    rp->rio_cnt   -= cnt;
    return (ssize_t)cnt;
}
```

`rio_readlineb`（第 79 行）在 `rio_read` 之上：

```c
static ssize_t rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen)
{
    size_t n;
    char   c, *bufp = usrbuf;
    for (n = 1; n < maxlen; n++) {
        ssize_t rc = rio_read(rp, &c, 1);   // 每次读 1 字节
        if (rc == 1) {
            *bufp++ = c;
            if (c == '\n') { n++; break; }  // 读到换行就停
        } else if (rc == 0) {
            if (n == 1) return 0;           // 一字节没读到 = EOF
            else break;
        } else {
            return -1;
        }
    }
    *bufp = 0;                              // 字符串结尾 '\0'
    return (ssize_t)(n - 1);
}
```

虽然看着每次读 1 字节很低效，但因为 RIO 内部有缓冲区，实际只是从内存 memcpy，效率没问题。

`rio_writen`（第 99 行）类似，循环 `write` 直到写满 n 字节，被信号打断也重试。

### 5.3 `parse_uri`（URL 转文件路径）

**位置**：`webserver.c:207`

**干嘛的**：浏览器发来的 URI 是 `/`、`/index.html`、`/foo/bar.css` 这种路径。服务器要把它转成机器上的真实文件路径，方法就是**前面拼上 root 目录**。

```c
static int parse_uri(const char *uri, const char *root,
                     char *filename, size_t buflen)
{
    if (strstr(uri, "..") != NULL) return -1;        // 安全检查

    char        tmp[2 * MAXLINE];
    const char *path = uri;

    if (uri[0] == '/' && uri[1] == '\0') {
        snprintf(tmp, sizeof(tmp), "%s/index.html", root);  // "/" → "/index.html"
    } else {
        if (uri[0] == '/') path = uri + 1;            // 去掉开头的 /
        snprintf(tmp, sizeof(tmp), "%s/%s", root, path);
    }

    if (strlen(tmp) >= buflen) return -2;             // 缓冲区不够
    strncpy(filename, tmp, buflen);
    filename[buflen - 1] = '\0';
    return 0;
}
```

**例子**：

| root | URI | 拼出的 filename |
|------|-----|----------------|
| `/home/ren/Desktop/CS/lab` | `/` | `/home/ren/Desktop/CS/lab/index.html` |
| `/home/ren/Desktop/CS/lab` | `/index.html` | `/home/ren/Desktop/CS/lab/index.html` |
| `/home/ren/Desktop/CS/lab` | `/css/a.css` | `/home/ren/Desktop/CS/lab/css/a.css` |

**为什么禁止 `..`**：如果允许，客户端可以发 `GET /../../../../etc/passwd HTTP/1.0`，服务器就会把系统密码文件发出去，这叫**路径穿越攻击 (path traversal)**。`strstr(uri, "..")` 一刀切，简单粗暴但绝对安全。

### 5.4 `get_mime_type`（文件类型 → Content-Type）

**位置**：`webserver.c:187`

**干嘛的**：浏览器收到响应后，靠 `Content-Type` 头判断这是什么类型的文件，决定怎么渲染（HTML 直接显示、图片显示图、CSS 应用样式……）。这个函数根据文件扩展名返回正确的 MIME 类型。

```c
static const char *get_mime_type(const char *filename)
{
    const char *dot = strrchr(filename, '.');  // 找最后一个 '.'
    if (!dot) return "application/octet-stream";
    if (!strcmp(dot, ".html") || !strcmp(dot, ".htm")) return "text/html";
    if (!strcmp(dot, ".css"))                           return "text/css";
    if (!strcmp(dot, ".js"))                            return "application/javascript";
    if (!strcmp(dot, ".txt"))                           return "text/plain";
    if (!strcmp(dot, ".png"))                           return "image/png";
    if (!strcmp(dot, ".jpg") || !strcmp(dot, ".jpeg"))  return "image/jpeg";
    if (!strcmp(dot, ".gif"))                           return "image/gif";
    if (!strcmp(dot, ".ico"))                           return "image/x-icon";
    return "application/octet-stream";         // 兜底：未知二进制
}
```

`application/octet-stream` 是「我不知道这是什么，浏览器你自己看着办」，通常会让用户下载。

### 5.5 `serve_static`（发送静态文件）

**位置**：`webserver.c:232`

**干嘛的**：把一个文件读出来，加上 HTTP 头，发给客户端。

```c
static void serve_static(int fd, const char *filename, int filesize)
{
    int srcfd = open(filename, O_RDONLY);              // 打开文件
    if (srcfd < 0) { /* 404 ... */ return; }

    char *srcbuf = malloc((size_t)filesize);           // 申请缓冲区
    if (!srcbuf) { /* 500 ... */ return; }

    ssize_t nread = read(srcfd, srcbuf, (size_t)filesize);  // 一次性读
    close(srcfd);
    if (nread != filesize) { /* 500 ... */ return; }

    char header[MAXBUF];
    int  hn = snprintf(header, sizeof(header),         // 拼头部
        "HTTP/1.0 200 OK\r\n"
        "Server: webserver\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        get_mime_type(filename), filesize);

    rio_writen(fd, header, (size_t)hn);                // 先发头部
    rio_writen(fd, srcbuf,  (size_t)filesize);         // 再发文件内容
    free(srcbuf);
}
```

**为什么先 malloc 再 read 而不是直接从 fd 读到网络**：因为 `read` 和 `write` 各是独立的系统调用，没办法原子地把文件内容转发到 socket。必须先读进内存，再写到网络。当然对于大文件这会占很多内存，可以用 `sendfile` 系统调用零拷贝，但本实验不追求那个。

**为什么头部和 body 分两次 `rio_writen`**：HTTP 协议规定状态行+头部+空行在前，body 在后。但 TCP 是流，无论你分几次 `write`，对端收到的字节序列是一样的。所以这里分两次只是代码清晰，从网络传输角度看跟一次写 `header + body` 没区别。

### 5.6 `client_error`（返回错误页）

**位置**：`webserver.c:160`

**干嘛的**：出错时返回一个简单的 HTML 错误页。格式跟 `serve_static` 一样，只是 body 是错误页 HTML。

```c
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
```

`errnum` 是状态码（404、403 等），`shortmsg` 是状态说明（Not Found、Forbidden 等），`cause` 是具体原因（哪个文件、哪个方法）。

### 5.7 `handle_request`（主处理流程）

**位置**：`webserver.c:301`

**干嘛的**：处理一个连接的完整流程。读请求 → 解析 → 服务 → 记日志。

```c
static void handle_request(int fd, const char *root,
                           struct sockaddr_in *clientaddr)
{
    rio_t rio;
    char  buf[MAXLINE];
    char  method[MAXLINE], uri[MAXLINE], version[MAXLINE];

    rio_readinitb(&rio, fd);
    if (rio_readlineb(&rio, buf, MAXLINE) <= 0) return;  // 读请求行

    if (sscanf(buf, "%63s %63s %63s", method, uri, version) < 3) {
        client_error(fd, buf, 400, "Bad Request", "...");   // 解析失败
        return;
    }

    if (strcasecmp(method, "GET") != 0) {                   // 非 GET
        client_error(fd, method, 501, "Not Implemented", "...");
        log_request("webserver.log", clientaddr, method, uri);
        return;
    }

    /* 读完剩余 header（读到空行止） */
    while (strcmp(buf, "\r\n") != 0) {
        if (rio_readlineb(&rio, buf, MAXLINE) <= 0) break;
    }

    char filename[MAXLINE];
    int  rc = parse_uri(uri, root, filename, sizeof(filename));
    if (rc == -1) { /* 403 路径穿越 */ return; }
    if (rc == -2) { /* 414 URI 太长 */ return; }

    struct stat st;
    if (stat(filename, &st) < 0)        { /* 404 文件不存在 */ return; }
    if (!S_ISREG(st.st_mode))           { /* 403 不是普通文件 */ return; }

    serve_static(fd, filename, (int)st.st_size);             // 发文件
    log_request("webserver.log", clientaddr, method, uri);   // 记日志
}
```

**为什么要把剩余 header 读完**：HTTP 协议规定客户端发完 header 才会等响应。如果服务器读到请求行就停，没把 header 读掉，TCP 缓冲区里会堆着客户端的 header 数据。虽然对这个简单服务器不影响响应，但读掉是规范做法。

**`stat` 是干嘛**：类似 `ls -l`，查询文件信息（大小、类型、权限），不打开文件。`S_ISREG` 判断是不是普通文件（不是目录、不是设备）。

### 5.8 `log_request`（写日志）

**位置**：`webserver.c:275`

**干嘛的**：往 `webserver.log` 文件追加一行记录。

```c
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

    FILE *fp = fopen(logpath, "a");          // 'a' = append
    if (!fp) return;
    fprintf(fp, "%s IP:%s %s %s\n", timestr, ip, method, uri);
    fflush(fp);
    fclose(fp);
}
```

**输出示例**（实际就是 `webserver.log` 里的内容）：

```
2026/07/21 12:39:33 IP:127.0.0.1 GET /
2026/07/21 12:39:33 IP:127.0.0.1 GET /favicon.ico
```

**为什么 `fopen("a")` 多进程安全**：`"a"` 模式对应 `O_APPEND`，POSIX 保证每次 `write` 都原子地追加到文件末尾。多个子进程同时写日志，内核会自动串行化，不会交错。

**为什么要 `fflush`**：C 标准库的 `FILE*` 有自己的缓冲区，`fprintf` 之后内容可能还在内存里没写盘。子进程马上要 `exit(0)`，如果不 `fflush`，缓冲区里的内容就丢了。`fflush` 强制刷盘。

### 5.9 `parse_config`（读配置文件）

**位置**：`webserver.c:393`

**干嘛的**：解析 `webserver.ini` 这种 `key=value` 格式的配置文件，提取 `root` 和 `port`。

`webserver.ini` 长这样：

```ini
# key=value 格式，# 开头为注释

root=/home/ren/Desktop/CS/lab
port=8080
```

代码逻辑：

```c
static int parse_config(const char *path, char *root, size_t root_len, int *port)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    char line[MAXLINE];
    while (fgets(line, sizeof(line), fp)) {       // 一行行读
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;                      // 砍掉注释
        trim(line);                               // 去首尾空白
        if (line[0] == 0) continue;               // 跳过空行

        char *eq = strchr(line, '=');             // 找 '='
        if (!eq) continue;
        *eq = 0;
        char *key = line;
        char *val = eq + 1;
        trim(key); trim(val);

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
```

`trim`（第 376 行）是个工具函数，把字符串前后的空格、tab、回车、换行都去掉：

```c
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
```

### 5.10 `sigchld_handler`（回收子进程）

**位置**：`webserver.c:364`

第 4.4 节已经讲过。完整代码：

```c
static void sigchld_handler(int sig)
{
    (void)sig;
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0) { /* silent reap */ }
    errno = saved_errno;
}
```

---

## 第 6 章 · 三个实验要求 ↔ 代码的对应关系

`requirements.txt` 里写了三个要求，对应到代码：

### 要求 1：基础 — 静态页面访问，多次请求不重启

**对应代码**：
- `main` 的 `while(1)` 死循环（保证不重启服务多个请求）
- `open_listenfd`（监听端口）
- `handle_request`（处理请求）
- `serve_static`（发文件）
- `client_error`（错误响应）
- `parse_uri`（URL 转路径）
- RIO 包（安全读写）
- `fork` + `sigchld_handler`（并发处理多个客户端）

### 要求 2：支持配置文件 `webserver.ini`

**对应代码**：
- `parse_config`（解析 `key=value`）
- `main` 里 `parse_config(cfg, root, ..., &port)` 这一段

### 要求 3：访问日志 `webserver.log`

**对应代码**：
- `log_request`（写一行日志）
- `handle_request` 里在每个分支（成功/404/403/501）末尾都调用它

---

## 第 7 章 · 怎么编译、运行

### 7.1 一键脚本（推荐）

```bash
./run.sh
```

`run.sh` 会自动：编译 → 杀掉旧进程 → 启动 → 打印访问地址 → 实时显示日志。Ctrl-C 退出。

也可以指定配置：

```bash
./run.sh my.ini
```

### 7.2 手动编译

```bash
make              # 编译，生成 ./webserver
./webserver       # 启动，默认读 ./webserver.ini
./webserver a.ini # 用指定的配置文件
make clean        # 清理产物
```

启动后控制台会输出：

```
[webserver] config loaded from webserver.ini
[webserver] root=/home/ren/Desktop/CS/lab, port=8080
[webserver] listening on port 8080
```

### 7.3 停止

在 `./webserver` 那个终端按 **Ctrl-C**。如果是 `./run.sh` 启动的，也是 Ctrl-C，脚本会自动转发信号给服务器。

---

## 第 8 章 · 怎么测试

### 8.1 用浏览器

打开浏览器，地址栏输入：

```
http://localhost:8080/
```

你应该能看到 `index.html` 的内容（虽然中文可能乱码，见第 9 章）。

### 8.2 用 curl（更精确）

```bash
# 看完整响应（包括头）
curl -v http://localhost:8080/

# 只看状态码
curl -o /dev/null -s -w "%{http_code}\n" http://localhost:8080/

# 测 404
curl -v http://localhost:8080/nope

# 测路径穿越（应返回 403）
curl -v --path-as-is http://localhost:8080/../requirements.txt

# 测不支持的方法（应返回 501）
curl -v -X POST http://localhost:8080/
```

### 8.3 看日志

```bash
tail -f webserver.log
```

每访问一次，日志文件就多一行。

### 8.4 测试并发

开 8 个终端同时跑 curl：

```bash
for i in 1 2 3 4 5 6 7 8; do curl -s -o /dev/null http://localhost:8080/ & done; wait
```

所有请求都应返回 200，且 `webserver.log` 多 8 行。

---

## 第 9 章 · 常见问题排查

### 9.1 "Address already in use"

端口被占。可能是上次的服务器没正常退出。解决：

```bash
pkill -x webserver          # 杀掉残留进程
# 或者
fuser -k 8080/tcp           # 直接杀占用 8080 的进程
```

如果还想确认是谁在占：

```bash
ss -tlnp | grep 8080
```

### 9.2 浏览器看到中文乱码

`index.html` 是 GBK 编码的，但 HTTP 头没指定字符集。两个解决办法：

1. 把 `index.html` 改成 UTF-8（推荐）：用编辑器另存为 UTF-8 即可。
2. 在 HTML `<head>` 里加一行：`<meta charset="gbk">`，告诉浏览器用 GBK 解码。

### 9.3 连不上

逐步检查：

1. **服务器真的在跑吗**？另一终端 `ps aux | grep webserver`。
2. **端口对吗**？`ss -tlnp | grep 8080`，应该能看到 `LISTEN`。
3. **防火墙挡了**？本地一般不会，但 `ufw status` 看一下。
4. **配置文件 port 字段对吗**？打开 `webserver.ini` 看。

### 9.4 404 但文件明明存在

检查 `webserver.ini` 里的 `root` 路径。如果你写了 `root=.` 但服务器是从别的目录启动的，`.` 就是那个目录而不是项目根。**用绝对路径最稳妥**：

```ini
root=/home/ren/Desktop/CS/lab
```

### 9.5 改了 .c 文件没生效

`run.sh` 有「源码比二进制新就重新编译」的逻辑，但偶尔会失效。强制重新编译：

```bash
make clean && make
```

### 9.6 看不到子进程退出但服务器越来越慢

可能是僵尸进程堆积，说明 `sigchld_handler` 没正常工作。检查：

```bash
ps aux | grep defunct
```

如果有大量 `defunct`（僵尸），看 `sigchld_handler` 和 `sigaction` 设置是否正确。

---

## 第 10 章 · 进阶扩展思路（不在本实验要求里）

如果你想继续深入，可以尝试：

- **改 pthread 并发**：用线程替代 fork，避免进程开销。
- **改 epoll 单线程事件循环**：单进程处理上万个连接，C10k 的标准方案。
- **支持 HTTPS**：用 OpenSSL 或 mbedTLS。
- **支持 CGI**：参考 CSAPP §11.6 `serve_dynamic`，能跑 PHP、Python 脚本。
- **支持 HTTP/1.1 keep-alive**：一个连接上跑多个请求。
- **支持 HEAD、POST 等方法**：HEAD 只返回头不发 body，POST 处理表单。
- **配置项扩展**：`max_clients`、`log_path`、`default_index`、`server_name` 等。

但这些都不是本实验要求。**先把上面三章要求和代码搞明白，再考虑这些**。

---

## 附录 · 关键概念速查表

| 概念 | 一句话解释 |
|------|-----------|
| HTTP | 客户端和服务器互发文本时遵守的格式 |
| socket | 内核提供的网络通信端点，用一个整数 fd 表示 |
| bind | 把 socket 关联到某个 IP + 端口 |
| listen | 标记 socket 为被动，可以接连接 |
| accept | 阻塞等连接，来一个返回一个新 fd |
| fork | 复制当前进程，返回两次 |
| SIGCHLD | 子进程退出时发给父进程的信号 |
| SIGPIPE | 往已关闭的 socket 写时触发的信号 |
| 僵尸进程 | 已退出但父进程还没收尸的进程 |
| RIO | CSAPP 的 robust I/O 包，处理短读和 EINTR |
| MIME | 文件类型标识，如 `text/html`、`image/png` |
| 路径穿越 | 用 `..` 逃出 web 根目录的攻击 |
