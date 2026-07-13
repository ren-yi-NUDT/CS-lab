# 计算机系统实验 10：Web 服务器

用纯 C 实现的简单 HTTP Web 服务器，基于 CSAPP §11.5/11.6。

## 功能

- 监听端口（默认 8080），服务静态页面
- 支持多浏览器并发访问（fork 模型）
- 读取 `webserver.ini` 配置文件（`root`、`port`）
- 访问日志 `webserver.log`（时间、IP、方法、路径）
- 安全：拒绝 `..` 目录穿越

## 一键启动

```bash
./run.sh                   # 编译 + 启动 + 打印访问信息，Ctrl-C 退出
./run.sh my.ini            # 用指定配置
```

## 手动编译 / 运行

```bash
make                       # 仅编译
./webserver                # 读 ./webserver.ini
./webserver my.ini         # 读指定配置
make clean                 # 清理产物与日志
```

## 测试

```bash
# 浏览器
xdg-open http://localhost:8080/   # 或直接打开浏览器输入

# curl
curl -v http://localhost:8080/
curl -v http://localhost:8080/index.html

# 看日志
tail -f webserver.log
```

## 配置示例 webserver.ini

```ini
# key=value 格式，# 开头为注释

root=/home/ren/Desktop/CS/lab
port=8080
```

## 文件清单

| 文件 | 说明 |
|------|------|
| `webserver_202402720028.c` | 服务器主程序（提交文件） |
| `webserver.ini` | 配置文件 |
| `Makefile` | 编译脚本 |
| `index.html` | 测试首页 |
| `requirements.txt` | 实验要求 |
| `计算机系统-实验10.pptx` | 实验讲义 |
| `helpme.md` | 技术说明 |
| `readme.md` | 快速上手（本文件） |

## 关键命令

```bash
make              # 编译
make run          # 编译并运行
make clean        # 清理产物与日志
./webserver       # 启动
curl localhost:8080   # 测试
```
