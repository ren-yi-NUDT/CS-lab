---
date: 2026-07-13
lab: 计算机系统实验10（Web 服务器）
branch: lab11
submission: webserver_202402720028.c
---

# 设计规格：Web 服务器实验

## 1. 实验任务

实现一个简单的 HTTP Web 服务器，能响应浏览器的静态页面请求。基于
CSAPP 第 11.5、11.6 节。三级要求：

1. **基本**：监听 80 端口；设定网站根目录；响应 GET 请求返回 `index.html`；
   能同时响应多个浏览器请求；纯 C 实现（不能用 Python / 高级库）。
2. **配置文件**：读取 `webserver.ini`（`key=value` 格式），获取 `root` 与 `port`。
3. **日志功能**：记录每个客户端的 IP、请求方法、请求路径与时间戳。

## 2. 架构

单文件 C 源码 + BSD sockets + `fork()` 并发。

```
main
 ├─ parse_args → 读 webserver.ini（默认路径，或 argv[1]）
 ├─ open_listenfd(port)
 ├─ signal(SIGCHLD, sigchld_handler)   // 异步回收僵尸进程
 ├─ signal(SIGPIPE, SIG_IGN)           // 客户端断开不杀服务器
 └─ loop:
       connfd = accept(listenfd, &addr, ...)
       if (fork() == 0) {              // 子进程
           close(listenfd);
           handle_request(connfd, root, &addr);
           exit(0);
       }
       close(connfd);                  // 父进程关闭连接副本
```

子进程 `handle_request` 流程：

```
1. rio_readinitb(&rio, connfd)
2. 读请求行：method / path / version
3. 跳过其余 header（读到空行止）
4. 安全检查：路径含 ".." → 403
5. 路径映射：'/' → '/index.html'；其余前缀拼接 root
6. 打开文件；失败 → 404
7. 写响应头 + 响应体
8. 写一行日志
9. close(connfd)
```

## 3. 关键决策

| 方面 | 决策 | 理由 |
|------|------|------|
| 并发模型 | `fork()` 每请求一进程 | CSAPP 教材风格，与 §11.5 一致 |
| I/O | CSAPP RIO（内联进 .c） | 防短读短写，不引入外部依赖 |
| HTTP 版本 | 以 HTTP/1.0 + `Connection: close` 响应 | 最简单且正确的连接关闭语义 |
| 配置文件 | 默认读 `./webserver.ini`，`argv[1]` 可覆盖 | 灵活，方便测试 |
| 配置缺失默认值 | `root="."`、`port=8080` | Linux 上 80 端口需要 root 权限 |
| 端口绑定失败 | 打印错误并 `exit(1)` | 包括 80 端口权限不足、端口已被占用等情况 |
| 日志 | 追加写 `./webserver.log`，每行一条 | `O_APPEND` 在 POSIX 下原子，多进程安全 |
| 路径安全 | 子串含 `..` 直接 403 | 防目录穿越 |
| MIME | html/css/js/png/jpg/gif/plain 二分查找 | 覆盖 index.html 场景；其他用 `application/octet-stream` |
| 信号 | `SIGCHLD` → 回收；`SIGPIPE` → 忽略 | 标准服务器实践 |

## 4. 文件与函数布局

```
webserver_202402720028.c
├── CSAPP 辅助（内联）
│   ├── rio_readinitb / rio_readlineb / rio_writen / rio_readnb
│   ├── open_listenfd   // socket/bind/listen 封装
│   └── open_clientfd（不需要）
├── 工具函数
│   ├── parse_config(path, &root, &port)
│   ├── get_mime_type(path) -> const char*
│   ├── parse_uri(uri, root, filename)
│   ├── serve_static(connfd, filename, size)
│   ├── client_error(connfd, cause, errnum, shortmsg, longmsg)
│   ├── log_request(ip_str, method, uri)
│   └── sigchld_handler(sig)
└── main(argc, argv)
```

## 5. 日志格式

```
2026/07/13 20:30:01 IP:127.0.0.1 GET /
2026/07/13 20:30:05 IP:192.168.1.5 GET /index.html
2026/07/13 20:30:10 IP:10.0.0.2 GET /style.css
```

- 时间戳：`localtime` + `strftime("%Y/%m/%d %H:%M:%S")`
- IP：`inet_ntoa` 转换
- 文件：以 `"a"` 模式 `fopen`，每条记录 `fprintf` 一行后 `fflush`

## 6. 错误响应（HTTP）

| 状态 | 触发条件 |
|------|----------|
| 200 OK | 文件正常返回 |
| 403 Forbidden | URI 含 `..` |
| 404 Not Found | 文件不存在或不可读 |
| 500 Internal Error | `fstat` 等系统调用失败 |

每个错误都返回一段 HTML 错误页让浏览器友好显示。

## 7. 交付清单

- `webserver_202402720028.c` — 主提交文件（必需）
- `webserver.ini` — Linux 友好配置示例：`root=/home/ren/Desktop/CS/lab`、`port=8080`
- `Makefile` — `make` 编译出 `webserver`；`make clean` 清理
- 保留原文件：`index.html`、`www.ini`、`requirements.txt`、`计算机系统-实验10.pptx`
- `readme.md` — 快速上手（编译 / 启动 / 测试 / 期望输出）
- `helpme.md` — 技术说明（架构、CSAPP 对应章节、关键问题、性能与扩展）

## 8. 测试方案

1. `make` → 编译无 warning
2. `./webserver` → 输出 `Web server listening on port 8080, root=/home/ren/Desktop/CS/lab`
3. 浏览器 `http://localhost:8080/` → 显示 index.html（中文乱码不影响 HTTP 正确性）
4. `curl -v http://localhost:8080/` → 响应头包含 `Content-Type: text/html`、`Content-Length`
5. `tail webserver.log` → 看到对应访问记录
6. 后台并发两个 `curl`，验证 fork 模型：两个连接都能成功返回
7. `curl http://localhost:8080/../requirements.txt` → 返回 403
8. `curl http://localhost:8080/nonexistent` → 返回 404

## 9. 范围之外（YAGNI）

明确不做：

- HTTPS / TLS
- HTTP/1.1 keep-alive
- POST / PUT 等非 GET 方法
- CGI / 动态内容
- 多线程（与 fork 取舍后选 fork）
- epoll / 事件循环
- 配置文件中的复杂语法（只支持 `key=value` 与 `#` 注释）
