# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

本实验是**实验3：超级缓冲区炸弹**——计算机安全课程的缓冲区溢出攻击实验。学生需要构造十六进制输入字符串，利用 `getbuf()` 的栈缓冲区溢出漏洞劫持控制流，完成4个难度递增的"木马"关卡。

仓库地址：`https://github.com/ren-yi-NUDT/CS-lab.git`，当前在 `lab3` 分支。

## 文件说明

- **`bufbomb.c`** — 漏洞程序源代码。核心函数 `getbuf()` 在栈上分配12字节缓冲区，通过 `getxs()` 读取十六进制输入，构成溢出攻击向量。
- **`bufbomb.exe`** — PE32（32位 Windows x86）可执行文件，是主要分析目标，需用 IDA Pro 反汇编获取地址。
- **`bufbomb_linux`** — ELF 64位 x86-64 可执行文件（源码注释中的地址为32位，如 `0x804c1e4`）。
- **`buflab.pdf`** — 实验指导书。
- **`README.md`** — 使用示例和各关卡预期输出。

## 运行方式

```bash
# 语法：./bufbomb.exe <学号后6位> [额外学号...]
# Linux 下使用 Wine：
wine bufbomb.exe <学号>

# Linux 原生二进制：
./bufbomb_linux <学号>
```

程序以学号数字作为参数初始化伪随机数生成器，推导出 `cookie` 值，然后在 stdin 等待十六进制编码的攻击字符串输入。`cookie` 值决定了什么是"正确"的攻击。

## 攻击关卡

| 关卡 | 目标函数 | 目标 |
|------|---------|------|
| 第1关 | `Trojan1()` | 覆盖返回地址，跳转到 `Trojan1`，无需传参。 |
| 第2关 | `Trojan2(val)` | 覆盖返回地址和栈上参数，使 `Trojan2` 接收到 `cookie` 作为参数。 |
| 第3关 | `Trojan3(val)` | 在栈上注入 shellcode：设置 `global_value = cookie`，然后跳转到 `Trojan3`。需要栈可执行。 |
| 第4关 | `Trojan4(val)` | 与第3关相同，但必须正常返回（不能调用 `exit(0)`），需要恢复栈帧。 |

## 关键地址（源自源码注释，需以实际反汇编为准）

源码 `bufbomb.c` 中注释的 shellcode 引用了以下32位地址：
- `0x804c1e4` — `cookie` 全局变量
- `0x804c1ec` — `global_value` 全局变量
- `0x8048ceb` — `Trojan3` 函数地址

务必对照实际反汇编结果验证地址，不同编译版本地址可能不同。

## 开发流程

1. 用 IDA Pro 反汇编 `bufbomb.exe`，获取当前函数/变量地址
2. 确定学号以计算 cookie 值
3. 构造十六进制攻击字符串（空格分隔的字节，从 stdin 输入）
4. 测试：`echo "<十六进制串>" | wine bufbomb.exe <学号>`
