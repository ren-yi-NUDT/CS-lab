# CS:APP Shell Lab — 实验报告

- **课程**: 计算机系统基础 (CS:APP)
- **实验**: Shell Lab — 实现带作业控制的小型 Shell
- **最终结果**: 16/16 个 trace 全部通过

---

## 实验目标

在骨架 `tsh.c` 上补齐 7 个核心函数，实现一个支持前台/后台任务运行、作业控制（`jobs`/`fg`/`bg`）、信号转发（Ctrl-C、Ctrl-Z）的小型 Unix Shell。

## 实现概要

所有修改集中在 `tsh.c`，补全了以下 7 个函数，其余骨架代码（`main`、`parseline`、任务列表操作等）未做修改。

### 1. `builtin_cmd` — 内置命令识别

处理 `quit`（退出）、`jobs`（列出所有作业）、`bg`/`fg`（作业前后台切换）、单独的 `&`（忽略）。非内置命令返回 0 交由 `eval` 处理。

### 2. `do_bgfg` — bg/fg 命令实现

支持 `%jid` 和 `pid` 两种参数格式，含完整错误处理（缺参数、无效 jid、无效 pid、格式错误）。关键是使用 `kill(-jobp->pid, SIGCONT)` 向整个进程组发送 `SIGCONT`，确保 `mysplit` 等 fork 出的子进程也一起恢复。

### 3. `eval` — 命令执行主入口

- 对非内置命令执行 `fork` + `execve`
- **临界区保护**：fork 前屏蔽 `SIGCHLD`，`addjob` 后解除，防止子进程秒退时 handler 在 `addjob` 前执行 `deletejob` 导致"幽灵任务"
- 子进程 `setpgid(0, 0)` 建立独立进程组，使 Ctrl-C/Ctrl-Z 只影响前台任务
- 后台任务打印 `[jid] (pid) cmdline` 后立即返回

### 4. `waitfg` — 等待前台进程

不调用 `waitpid`（回收已在 handler 中完成），而是用 `sigsuspend` 挂起直到前台进程 ID 不再是目标 pid。相比 `sleep(1)` 忙等，`sigsuspend` 响应及时且不浪费 CPU。

### 5. `sigchld_handler` — 子进程状态变更处理

- `waitpid(-1, ..., WNOHANG | WUNTRACED)` 非阻塞轮询
- 用 `while` 而非 `if`，一次性回收多个同时退出的子进程
- `WIFEXITED` → 删除任务，`WIFSIGNALED` → 打印终止信息后删除，`WIFSTOPPED` → 改状态为 ST 但不删除
- 保存/恢复 `errno` 避免污染被打断的系统调用

### 6. `sigint_handler` — Ctrl-C 信号转发

向 `fgpid(jobs)` 对应的进程组发送 `kill(-pid, SIGINT)`。无前台任务时不操作，避免误杀 shell 自身。

### 7. `sigtstp_handler` — Ctrl-Z 信号转发

与 `sigint_handler` 对称，向进程组发送 `kill(-pid, SIGTSTP)`。子进程收到后暂停，内核触发 `SIGCHLD`，由 handler 将任务状态改为 ST。

## 关键设计要点

| 要点 | 说明 |
|------|------|
| 进程组隔离 | `setpgid(0, 0)` 保证前台任务独立进程组，信号不误伤 shell |
| SIGCHLD 临界区 | fork 前屏蔽 → addjob 后解除，防止竞态 |
| kill(-pgid, sig) | 用负号 PID 向整个进程组发信号，mysplit 的父子一起处理 |
| sigsuspend | 比忙等更高效、更可靠的同步等待方式 |
| while waitpid | handler 中循环 reap，处理信号合并 |
| errno 保存/恢复 | handler 中不污染主流程的系统调用 errno |

## 测试结果

运行 `./run_traces.sh`，16 个 trace 全部通过：

```
Trace      Result   Description
trace01    PASS     Properly terminate on EOF.
trace02    PASS     Process builtin quit command.
trace03    PASS     Run a foreground job.
trace04    PASS     Run a background job.
trace05    PASS     Process jobs builtin command.
trace06    PASS     Forward SIGINT to foreground job.
trace07    PASS     Forward SIGINT only to foreground job.
trace08    PASS     Forward SIGTSTP only to foreground job.
trace09    PASS     Process bg builtin command
trace10    PASS     Process fg builtin command.
trace11    PASS     Forward SIGINT to every process in foreground process group
trace12    PASS     Forward SIGTSTP to every process in foreground process group
trace13    PASS     Restart every stopped process in process group
trace14    PASS     Simple error handling
trace15    PASS     Putting it all together
trace16    PASS     Tests whether the shell can handle SIGTSTP and SIGINT

=== 总结: 16 passed, 0 failed ===
```

## 编译与运行

```bash
cd shlab-handout
make                    # 编译 tsh + 测试程序 (myspin/mysplit/mystop/myint)

# 交互式运行
./tsh

# 运行单个 trace
./sdriver.pl -t trace05.txt -s ./tsh -a "-p"

# 并行跑全部 16 个 trace
./run_traces.sh
```

## 文件说明

| 文件 | 说明 |
|------|------|
| `tsh.c` | 自己实现的 Tiny Shell（核心交付） |
| `tshref` | 参考实现，用于对比 |
| `run_traces.sh` | 自写的并行测试脚本（PID 归一化 + 边跑边展示） |
| `trace01~16.txt` | sdriver 驱动脚本，定义测试流程 |
| `sdriver.pl` | trace 驱动器，模拟终端输入 |
| `myspin.c` / `mysplit.c` / `mystop.c` / `myint.c` | 测试用小程序 |

## 实验心得

本实验核心在于理解 **信号 + 进程组 + 临界区** 三者的协同：

- **进程组** 使得 Ctrl-C/Ctrl-Z 能精准命中前台任务而不波及 shell 自身；
- **信号机制** 让 shell 能异步感知子进程的退出/停止，无需轮询；
- **临界区保护**（fork 前屏蔽 SIGCHLD）防止了 job list 状态与子进程实际状态的不一致。

这是第一次"从系统调用层面"理解 shell 的工作原理，对进程控制、信号处理、并发竞态有了直观的认识。
