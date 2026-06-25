# Shell Lab Report

> 目标：把 `shlab-handout/tsh.c` 这个只有空壳的"玩具 shell"补全，让它能像参考实现 `tshref` 一样，跑通全部 16 个 trace 测试。


---

## 0. 一句话总览

我们要做的事情只有一件：**给一个骨架 shell 加上"任务管理"能力**——能让用户在前台跑命令、把命令丢到后台、用 `jobs` 查看、用 `fg/bg` 把任务拉到前台或丢回后台、用 `Ctrl-C`/`Ctrl-Z` 控制前台任务。

骨架已经写好了 `main`、命令行解析（`parseline`）、任务列表（`addjob`/`deletejob`/`listjobs` 等）。**我们只需要实现 7 个函数**。

| 函数 | 干啥 |
|---|---|
| `eval` | 主入口：解析命令、`fork` 子进程、把任务塞进 jobs 列表 |
| `builtin_cmd` | 识别内置命令：`quit` / `jobs` / `bg` / `fg` / `&` |
| `do_bgfg` | 实现 `bg` / `fg` 命令本体 |
| `waitfg` | 等待前台进程结束 |
| `sigchld_handler` | 子进程结束/停止时，内核发 SIGCHLD，这里回收它 |
| `sigint_handler` | Ctrl-C 时把 SIGINT 转发给前台进程组 |
| `sigtstp_handler` | Ctrl-Z 时把 SIGTSTP 转发给前台进程组 |

---

## 1. 准备工作

### 1.1 环境

必须 Linux。Windows 用户请用 WSL、VirtualBox/VMware 虚拟机，或者 educoder 平台。macOS 不行（trace 文件用 `ps a` 查进程，和 Linux 的输出格式不同）。

确认工具链：

```bash
gcc --version    
perl -v          # 跑 sdriver.pl 用
make --version
```

### 1.2 文件清单

```
shlab-handout/
├── tsh.c          ← 我们要改这个
├── tshref         ← 参考实现（只读，不能改）
├── tshref.out     ← 参考实现的 16 个 trace 期望输出
├── Makefile       ← make test01..test16 / make rtest01..rtest16
├── sdriver.pl     ← trace 驱动器
├── trace01.txt … trace16.txt   ← 16 个测试脚本
├── myspin.c       ← "转 N 秒" 测试程序
├── mysplit.c      ← "fork 一个子进程一起转" 测试程序
├── mystop.c       ← "转 N 秒后给自己发 SIGTSTP"
└── myint.c        ← "转 N 秒后给自己发 SIGINT"
```

### 1.3 第一次编译

```bash
cd shlab-handout
make
```

应该生成 5 个可执行文件：`tsh`、`myspin`、`mysplit`、`mystop`、`myint`。此时 `./tsh` 能起来，但任何命令都跑不了（因为 `eval` 是空壳）。

### 1.4 怎么手动玩 tsh

```bash
./tsh
tsh> ./myspin 3       # 前台跑 3 秒（会卡住，因为没有 waitfg）
tsh> ./myspin 3 &     # 想后台跑（但啥也不打印，因为 eval 是空的）
tsh> jobs             # 想看任务（但 builtin_cmd 也是空的）
```

### 1.5 怎么用 trace 跑测试

```bash
# 单个 trace
./sdriver.pl -t trace05.txt -s ./tsh -a "-p"

# 对照参考实现
./sdriver.pl -t trace05.txt -s ./tshref -a "-p"

# 或用 Makefile
make test05      # 跑你的 tsh
make rtest05     # 跑 tshref
```

`-a "-p"` 表示给 shell 传 `-p` 参数，意思是"别打印 `tsh>` 提示符"——因为 trace 文件自己用 `/bin/echo` 打印了 `tsh>`，避免重复。

---

## 2. 必备背景知识

如果下面任何一项你完全没概念，建议先看 CS:APP 第 8 章。这里只做"够用"的复习。

### 2.1 进程基础

- `fork()`：把当前进程复制一份。返回值：父进程拿到子进程 PID，子进程拿到 0。
- `execve(path, argv, envp)`：把当前进程的内存替换成 `path` 程序。**PID 不变**。成功不返回。
- `waitpid(pid, &status, flags)`：等指定子进程结束，拿到它的退出状态。
- `exit(n)`：进程结束，退出码 `n`。

### 2.2 进程组（**这个 lab 最关键的概念**）

每个进程属于一个"进程组"。`kill(-pgid, sig)` 会把信号发给**整个组**的所有进程。

shell 启动一个前台任务时，必须把这个任务（以及它 fork 出的子任务）放进**同一个独立的进程组**里，组 ID 通常等于这个任务的主 PID。这样 shell 按 Ctrl-C 时，信号只发给这个组，不会误伤 shell 自己或其他后台任务。

把子进程放进新组只需要一行：

```c
setpgid(0, 0);   // 0,0 表示"把我自己放进一个新组，组 ID 等于我的 PID"
```

### 2.3 信号

| 名字 | 触发 | 含义 |
|---|---|---|
| `SIGINT` (2)  | Ctrl-C | 中断 |
| `SIGTSTP` (20) | Ctrl-Z | 暂停（停止） |
| `SIGCHLD` | 子进程结束/停止 | 内核通知父进程 |
| `SIGCONT` | `kill -CONT` | 让暂停的进程继续 |
| `SIGQUIT` | Ctrl-\ | 退出（shell 自己用） |

注册信号 handler 用 `sigaction`，骨架里给了 `Signal()` 包装。

### 2.4 任务状态机

```
                fork + exec
   ┌────────────────────────────────┐
   │                                ▼
   │                            ┌──────┐
   │              Ctrl-Z        │  FG  │  ← 前台（只能 1 个）
   │            ┌───────────────┤      │
   │            │               └──────┘
   │            ▼                  ▲
   │        ┌──────┐  bg %n       │  fg %n
   └────────┤  ST  │─────────────►├──────┤
            │ 停止 │              │  BG  │  ← 后台（可以多个）
            └──────┘              └──────┘
                ▲                     │
                └──────Ctrl-Z─────────┘
```

`fg %n` 把任务从 ST/BG 变成 FG；`bg %n` 把任务从 ST 变成 BG。

---

## 3. 七个函数逐一实现

下面**每个函数都给完整代码 + 设计理由**。建议你跟着一行行写一遍，比直接复制粘贴学得多。

### 3.1 `builtin_cmd` —— 最简单的，先做这个

```c
int builtin_cmd(char **argv)
{
    if (!strcmp(argv[0], "quit"))
        exit(0);                          // 直接退出 shell
    if (!strcmp(argv[0], "&"))
        return 1;                         // 单独一个 & 啥也不做
    if (!strcmp(argv[0], "jobs")) {
        listjobs(jobs);                   // 骨架已提供
        return 1;
    }
    if (!strcmp(argv[0], "bg") || !strcmp(argv[0], "fg")) {
        do_bgfg(argv);
        return 1;
    }
    return 0;   // 不是内置命令，返回 0 让 eval 去 fork
}
```

**为什么这样写**：
- `quit` / `jobs` / `bg` / `fg` 是 shell 自己要处理的命令（不能 fork 一个叫 `quit` 的程序出来）。
- 单独的 `&`（用户输入一行就是 `&`）没意义，忽略即可。
- 返回值约定：1 表示"我处理了"，0 表示"不是内置命令"。

### 3.2 `do_bgfg` —— bg/fg 命令

参数格式：`bg %2`（按 jid）、`bg 12345`（按 pid）

```c
void do_bgfg(char **argv)
{
    struct job_t *jobp = NULL;
    char *id = argv[1];

    // 1. 参数检查
    if (id == NULL) {
        printf("%s command requires PID or %%jobid argument\n", argv[0]);
        return;
    }

    // 2. 按 %jid 还是 pid 查 job
    if (id[0] == '%') {
        int jid = atoi(id + 1);
        jobp = getjobjid(jobs, jid);
        if (jobp == NULL) {
            printf("%%%d: No such job\n", jid);
            return;
        }
    } else if (isdigit((unsigned char)id[0])) {
        pid_t pid = atoi(id);
        jobp = getjobpid(jobs, pid);
        if (jobp == NULL) {
            printf("(%d): No such process\n", pid);
            return;
        }
    } else {
        printf("%s: argument must be a PID or %%jobid\n", argv[0]);
        return;
    }

    // 3. 给整个进程组发 SIGCONT（让它从停止状态继续）
    kill(-jobp->pid, SIGCONT);

    // 4. 区分 fg / bg
    if (!strcmp(argv[0], "fg")) {
        jobp->state = FG;
        waitfg(jobp->pid);            // 前台：等它跑完
    } else {
        jobp->state = BG;
        printf("[%d] (%d) %s", jobp->jid, jobp->pid, jobp->cmdline);
    }
}
```

**为什么用 `kill(-pid, SIGCONT)` 而不是 `kill(pid, SIGCONT)`**：
- 用 `-pid` 表示发给"以 pid 为组长的整个进程组"。
- 如果任务是用 `mysplit` 启动的（它会再 fork 一个子进程），父子的 pid 不同但同组。
- 用 `-pid` 确保整组都被唤醒。

**错误信息要逐字符匹配**：trace14 专测这个。漏一个冒号、少一个空格都会 diff 失败。

### 3.3 `waitfg` —— 等前台进程结束

```c
void waitfg(pid_t pid)
{
    sigset_t mask;
    sigemptyset(&mask);
    while (fgpid(jobs) == pid)
        sigsuspend(&mask);    // 挂起，等任意信号到来
}
```

**为什么不用 `waitpid`**：
- 我们已经在 `sigchld_handler` 里用 `waitpid` 回收子进程了。
- 父进程在 `waitfg` 里如果再 `waitpid` 同一个子进程，会出错（子进程已经被回收了）。
- 所以 `waitfg` 的正确姿势是：**只检查"前台任务还在不在 jobs 列表里"**，不在了就返回。

**为什么用 `sigsuspend` 而不是 `sleep(1)`**：
- `sleep(1)` 是忙等：每秒醒一次看一眼。响应延迟最差 1 秒。
- `sigsuspend(&mask)` 是"原子地解除所有信号屏蔽并挂起，等任何信号到来后返回"。
- 当 SIGCHLD 来了 → handler 跑完 → `sigsuspend` 返回 → 检查 `fgpid` → 若不再是前台 pid，退出循环。

### 3.4 `eval` —— 主入口，最复杂

```c
void eval(char *cmdline)
{
    char *argv[MAXARGS];
    char buf[MAXLINE];
    int bg;
    pid_t pid;
    sigset_t mask_chld, prev_chld;

    strcpy(buf, cmdline);
    bg = parseline(buf, argv);

    if (argv[0] == NULL)
        return;                          // 空行

    if (builtin_cmd(argv))
        return;                          // 内置命令已处理

    // ★ 关键：fork 前屏蔽 SIGCHLD
    sigemptyset(&mask_chld);
    sigaddset(&mask_chld, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask_chld, &prev_chld);

    if ((pid = fork()) == 0) {           // 子进程
        sigprocmask(SIG_SETMASK, &prev_chld, NULL);  // 子进程解除屏蔽
        setpgid(0, 0);                   // ★ 关键：放进新进程组
        if (execve(argv[0], argv, environ) < 0) {
            printf("%s: Command not found\n", argv[0]);
            fflush(stdout);
            exit(0);
        }
    }

    // 父进程
    addjob(jobs, pid, bg ? BG : FG, cmdline);
    sigprocmask(SIG_SETMASK, &prev_chld, NULL);   // ★ 解除 SIGCHLD 屏蔽

    if (!bg) {
        waitfg(pid);
    } else {
        printf("[%d] (%d) %s", pid2jid(pid), pid, cmdline);
        fflush(stdout);
    }
}
```

#### 为什么 fork 前要屏蔽 SIGCHLD？（**这个 lab 的灵魂**）

设想不屏蔽会发生什么：

```
1. 父进程 fork
2. 子进程瞬间 exec ./myspin，然后立刻退出（假如 ./myspin 0）
3. 内核给父进程发 SIGCHLD
4. sigchld_handler 跑起来，调 deletejob，但 jobs 里根本没这个 pid！
   （因为父进程还没执行 addjob）
5. handler 返回
6. 父进程继续执行 addjob —— 把一个已经死了的进程加进 jobs
7. jobs 列表里永远有一个"僵尸任务"
```

**正确做法**：fork 前屏蔽 SIGCHLD → fork → 父进程 addjob → 解除屏蔽。这样即便子进程秒退，SIGCHLD 也只能"排队等着"，等父进程 addjob 完成并解除屏蔽后才会被处理。

#### 为什么子进程要解除屏蔽？

子进程继承父进程的屏蔽状态。如果带着屏蔽的 SIGCHLD 去 exec，新程序如果也用 SIGCHLD 就会出 bug。所以子进程第一件事就是恢复原屏蔽状态。

#### 为什么 `setpgid(0, 0)`？

如前 §2.2 所述：把子进程放进独立进程组，这样 Ctrl-C 只杀前台任务，不杀 shell。

#### 为什么打印用 `%s` 直接打 `cmdline`？

`cmdline` 是从 `fgets` 来的，**末尾自带 `\n`**。所以 `printf("[%d] (%d) %s", ...)` 就够了，不用再加 `\n`。看 trace04 的期望输出：

```
[1] (26252) ./myspin 1 &
```

末尾换行就是 cmdline 自带的。

### 3.5 `sigchld_handler` —— 回收僵尸

```c
void sigchld_handler(int sig)
{
    int olderr = errno;                  // ★ 保存 errno
    pid_t pid;
    int status;

    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        if (WIFEXITED(status)) {
            deletejob(jobs, pid);        // 正常退出，删
        } else if (WIFSIGNALED(status)) {
            int jid = pid2jid(pid);
            printf("Job [%d] (%d) terminated by signal %d\n",
                   jid, pid, WTERMSIG(status));
            fflush(stdout);
            deletejob(jobs, pid);        // 被信号杀掉，删
        } else if (WIFSTOPPED(status)) {
            struct job_t *jp = getjobpid(jobs, pid);
            if (jp) {
                jp->state = ST;          // 改状态为停止
                printf("Job [%d] (%d) stopped by signal %d\n",
                       jp->jid, pid, WSTOPSIG(status));
                fflush(stdout);
            }
        }
    }

    errno = olderr;                      // ★ 恢复 errno
}
```

**逐行解释**：

- **保存/恢复 `errno`**：信号 handler 可能中断一个正在用 `errno` 的系统调用，handler 跑完 errno 被污染了。所以进来先存、出去再恢复。
- **`waitpid(-1, ..., WNOHANG | WUNTRACED)`**：
  - `-1`：等任意子进程。
  - `WNOHANG`：没有就立刻返回 0（别阻塞 handler）。
  - `WUNTRACED`：子进程**停止**（被 SIGTSTP）也汇报。
- **`while` 而不是 `if`**：一次 SIGCHLD 可能对应多个子进程结束（内核会合并信号），所以要循环 reap。
- **三个分支**：
  - `WIFEXITED`：子进程 `return` / `exit()` 了，悄悄删任务即可。
  - `WIFSIGNALED`：被 SIGINT/SIGKILL 杀了，**要先打印通知，再删**（否则 `pid2jid` 拿不到 jid）。
  - `WIFSTOPPED`：被 SIGTSTP 暂停了，**改状态为 ST，不删任务**（任务还在，只是停了）。

### 3.6 `sigint_handler` —— Ctrl-C 转发

```c
void sigint_handler(int sig)
{
    int olderr = errno;
    pid_t pid = fgpid(jobs);
    if (pid != 0)
        kill(-pid, SIGINT);              // ★ -pid：发给整个进程组
    errno = olderr;
}
```

**逻辑**：
- 找到当前前台进程 pid。
- `kill(-pid, SIGINT)` 把 SIGINT 发给"以 pid 为组长的进程组"。
- 如果没有前台任务（用户在 shell 提示符下按 Ctrl-C），啥也不做（保持 shell 不被杀）。

### 3.7 `sigtstp_handler` —— Ctrl-Z 转发

```c
void sigtstp_handler(int sig)
{
    int olderr = errno;
    pid_t pid = fgpid(jobs);
    if (pid != 0)
        kill(-pid, SIGTSTP);
    errno = olderr;
}
```

和 `sigint_handler` 完全对称，只是把 SIGINT 换成 SIGTSTP。子进程收到 SIGTSTP 后会暂停，然后内核也会给父进程发 SIGCHLD（带 `WUNTRACED`），由 `sigchld_handler` 改状态为 ST 并打印 "stopped by signal 20"。

---

## 4. 编译 & 跑测试

### 4.1 编译

```bash
cd shlab-handout
make clean && make
```

应当无 warning。如果有，仔细看。

### 4.2 跑单个 trace

```bash
./sdriver.pl -t trace05.txt -s ./tsh -a "-p"
```

### 4.3 和参考实现对比

```bash
diff <(./sdriver.pl -t trace05.txt -s ./tsh -a "-p") \
     <(./sdriver.pl -t trace05.txt -s ./tshref -a "-p")
```

**注意**：每行 PID 数字会不同（每次跑 pid 不一样），但其他内容必须完全一致。

### 4.4 一键跑全部 16 个 trace（PID 归一化）

```bash
norm() { sed -E 's/\(([0-9]+)\)/(PID)/g; s/signal [0-9]+/signal N/g'; }
for i in $(seq -w 1 16); do
  mine=$(./sdriver.pl -t trace$i.txt -s ./tsh -a "-p" 2>&1 | norm)
  ref=$(./sdriver.pl -t trace$i.txt -s ./tshref -a "-p" 2>&1 | norm)
  if [ "$mine" = "$ref" ]; then echo "trace$i: PASS"; else echo "trace$i: DIFF"; diff <(echo "$mine") <(echo "$ref"); fi
done
```

期望输出：

```
trace01: PASS
trace02: PASS
...
trace16: PASS
```

> ⚠️ trace11/12/13 里包含 `/bin/ps a`，会列出系统所有进程。如果你机器上同时有别的程序在跑（浏览器、git 等），那几行会 diff，但**这不是 shell 的问题**，是环境噪声。

### 4.5 提交

```bash
cp tsh.c tsh_202402720028.c    # 学号替换为你自己的
```

---

## 5. 16 个 trace 在测什么

| trace | 测试点 | 实现关键 |
|---|---|---|
| 01 | EOF 时正常退出 | 骨架已实现 |
| 02 | `quit` 内置命令 | `builtin_cmd` |
| 03 | 前台任务 | `eval` + `waitfg` |
| 04 | 后台任务，打印 `[jid] (pid) cmdline` | `eval` 的 bg 分支 |
| 05 | `jobs` 内置命令 | `builtin_cmd` + `listjobs` |
| 06 | Ctrl-C 杀前台 | `sigint_handler` |
| 07 | Ctrl-C 只杀前台不杀后台 | `setpgid` + `kill(-pid)` |
| 08 | Ctrl-Z 暂停前台 | `sigtstp_handler` + `WUNTRACED` |
| 09 | `bg %n` 后台恢复 | `do_bgfg` 的 bg 分支 |
| 10 | `fg %n` 前台恢复 | `do_bgfg` 的 fg 分支 |
| 11 | Ctrl-C 杀整个进程组（mysplit） | `kill(-pid, SIGINT)` |
| 12 | Ctrl-Z 暂停整个进程组 | `kill(-pid, SIGTSTP)` |
| 13 | fg 恢复整个停止的进程组 | `kill(-pid, SIGCONT)` |
| 14 | 错误处理 | `do_bgfg` 的各种错误信息 |
| 15 | 综合测试 | 全部 |
| 16 | 信号来自其他进程而非终端 | `sigchld_handler` 处理 self-signal |

---

## 6. 常见坑（按踩坑概率排序）

### 坑 1：忘了 `setpgid(0, 0)` —— Ctrl-C 把 shell 自己也杀了

**症状**：trace06 一跑，整个 shell 退出，连"Job [1] terminated by signal 2"都没打印。

**原因**：fork 出来的子进程默认继承 shell 的进程组。`kill(pid, SIGINT)` 或 `kill(-shell_pgid, SIGINT)` 都会把 shell 自己也杀掉。

**解决**：子进程第一件事 `setpgid(0, 0)`。

### 坑 2：忘了屏蔽 SIGCHLD —— jobs 列表出现幽灵

**症状**：trace05 跑 `jobs`，列出一个早该消失的任务。

**原因**：见 §3.4 "为什么 fork 前要屏蔽 SIGCHLD"。

**解决**：fork 前 `sigprocmask(SIG_BLOCK, ...)`，addjob 后 `sigprocmask(SIG_SETMASK, ...)`。

### 坑 3：`sigchld_handler` 里只用 `if` 不用 `while` —— 漏回收

**症状**：偶发性 jobs 列表残留。

**原因**：多个子进程几乎同时结束，内核只发一个 SIGCHLD。`if` 只回收一个，剩下的成僵尸。

**解决**：`while ((pid = waitpid(...)) > 0)` 循环到没有为止。

### 坑 4：先 `deletejob` 再 `pid2jid` —— jid 永远是 0

**症状**：trace06 输出 `Job [0] (xxxx) terminated by signal 2`。

**原因**：

```c
deletejob(jobs, pid);                // 先删
printf("...jid=%d...", pid2jid(pid)); // pid2jid 找不到，返回 0
```

**解决**：先打印（pid2jid 还能找到），再 delete。

### 坑 5：错误信息差一个字符

**症状**：trace14 卡在某一行的 diff。

**原因**：参考实现的错误信息长这样：

```
fg command requires PID or %jobid argument
fg: argument must be a PID or %jobid
(9999999): No such process
%2: No such job
```

注意 `%` 在 printf 里要写成 `%%`。

**解决**：仔细对照 `tshref.out` 的 trace14 段，逐字符抄。

### 坑 6：在 handler 里用 `printf` 不 `fflush`

**症状**：输出偶尔丢一行。

**原因**：`printf` 是行缓冲（stdout 是终端时）或全缓冲（被 sdriver 重定向时）。在 handler 里没 flush，主程序退出时才冲掉，但顺序可能错。

**解决**：handler 里每次 `printf` 后 `fflush(stdout)`。

### 坑 7：`waitfg` 用 `sleep(1)` —— 测试慢、不稳

**症状**：能跑通，但每条 trace 慢 1 秒，而且偶发竞争。

**解决**：用 `sigsuspend`，见 §3.3。

---

## 7. 进程组、信号、jobs 三者怎么协同的（图解）

以 trace09 为例，命令序列：

```
./myspin 4 &      # BG 任务 jid=1
./myspin 5        # FG 任务 jid=2，5 秒
TSTP              # 内核给 shell 发 SIGTSTP（trace 里 sdriver 帮忙发的）
jobs              # 应该看到 [1] Running + [2] Stopped
bg %2             # 把 jid=2 拉到后台继续跑
jobs              # 应该看到 [1] Running + [2] Running
```

执行时间线：

```
shell              myspin(4) [BG, pgid=Pid1]    myspin(5) [FG, pgid=Pid2]
  │                       │                            │
  │── fork+exec ─────────►│                            │
  │── fork+exec ──────────┼───────────────────────────►│
  │                       │                            │
  │  TSTP 来了             │                            │
  │  sigtstp_handler:      │                            │
  │   pid = fgpid = Pid2   │                            │
  │   kill(-Pid2, SIGTSTP) │◄────── SIGTSTP ───────────│ (暂停)
  │                       │                            │
  │  SIGCHLD 来了           │                            │
  │  sigchld_handler:      │                            │
  │   waitpid → WIFSTOPPED │                            │
  │   jobs[2].state = ST   │                            │
  │   print "stopped"      │                            │
  │                       │                            │
  │── bg %2 → do_bgfg ─────│───────────────────────────►│
  │   kill(-Pid2, SIGCONT) │                            │ (恢复)
  │   jobs[2].state = BG   │                            │
  │   print "[2] (Pid2)..."│                            │
```

关键观察：
- shell 自己**从不直接 wait 子进程的退出码**——这活儿全交给 `sigchld_handler`。
- shell 改 jobs 状态后，handler 在下次 SIGCHLD 时自然完成清理。
- 信号通过**进程组**广播，所以 `mysplit` fork 出的孙子进程也能被一起管控。

---

## 8. 最终文件清单

提交前确认：

- [ ] `shlab-handout/tsh.c` 已经按 §3 实现 7 个函数
- [ ] `make` 无 warning
- [ ] 16 个 trace 全 PASS（PID 归一化对比）
- [ ] 复制为 `tsh_<你的学号>.c`
- [ ] 不要修改骨架的 `parseline`、`addjob`、`deletejob`、`listjobs` 等已有函数
- [ ] 不要改 `prompt[]`、`main` 函数

---

## 9. 一句话总结

**这个 lab 就是在练"信号 + 进程组 + 临界区"三件事。**

- 进程组让信号能精准命中目标；
- 信号让 shell 能异步响应子进程变化；
- 屏蔽 SIGCHLD 让 fork/addjob 这个"临界区"不被打断。

把这三件事想清楚，7 个函数自然就写出来了。
