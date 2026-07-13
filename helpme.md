# Web 服务器实验 · 技术说明

> `webserver_202402720028.c` 的设计与实现细节。CSAPP §11.5/11.6 风格。

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
| 穿越攻击 | `curl --path-as-is http://localhost:8080/../x` | 403 |
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

## 7. 编译与运行

```bash
# 一键编译
make

# 启动（读 ./webserver.ini）
./webserver

# 或指定配置文件
./webserver /path/to/my.ini
```

启动后控制台输出：

```
[webserver] config loaded from webserver.ini
[webserver] root=/home/ren/Desktop/CS/lab, port=8080
[webserver] listening on port 8080
```

## 8. 实测结果（2026-07-13）

在本机（i9-14900HX, Linux 6.17）实测：

```
=== Test 1: GET / (200) ===
HTTP 200, size 272 bytes

=== Test 2: 8 sequential requests (no restart) ===
  request 1: 200
  ...
  request 8: 200

=== Test 3: 8 concurrent requests ===
1:200 4:200 2:200 3:200 6:200 5:200 8:200 7:200

=== Test 4: 404 / 403 / 501 ===
  not found: 404
  traversal: 403
  POST: 501
```

并发 8 个请求全部成功，无僵尸进程残留。

## 9. 扩展思路（不在本实验范围）

- 改 pthread：避免 fork 开销
- 改 epoll：单线程事件循环
- 支持 HTTPS：openssl / mbedTLS
- 支持 CGI：参考 CSAPP §11.6 serve_dynamic
- 支持 keep-alive：HTTP/1.1
- 配置项扩展：max_clients、log_path、default_index、server_name 等
