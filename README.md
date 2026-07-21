# 计算机系统课程实验合集 · CS:APP Labs

> 李好，这里是计算机系统课上代码 & 大作业的 repo ✨
>
> 全部 12 个实验已更新完毕，每个实验独立分支存放，互不污染。

---

## 📚 实验导航

每个实验对应一个独立分支，点击「分支」列直达对应代码，点击「在线 README」直达该分支的说明文档。

| #   | 实验名称             | 关键技术                          | 难度     | 分支     | 在线 README                                                                                       |
| --- | -------------------- | --------------------------------- | -------- | -------- | ------------------------------------------------------------------------------------------------- |
| 1   | 数据实验 · 位运算    | 位运算、补码、IEEE 浮点           | ★★☆☆☆   | `lab1`   | [查看](https://github.com/ren-yi-NUDT/CS-lab/blob/lab1/README.txt)                                |
| 2   | 二进制炸弹           | x86 反汇编、GDB、逆向工程         | ★★★☆☆   | `lab2`   | [查看](https://github.com/ren-yi-NUDT/CS-lab/blob/lab2/README.md)                                 |
| 3   | 缓冲区炸弹           | 栈帧、栈溢出、ROP                 | ★★★☆☆   | `lab3`   | [查看](https://github.com/ren-yi-NUDT/CS-lab/blob/lab3/README.md)                                 |
| 4   | Y86-64 指令模拟器    | Y86-64 ISA、流水线                | ★★★★☆   | `lab4`   | [查看](https://github.com/ren-yi-NUDT/CS-lab/blob/lab4/README.md)                                 |
| 5   | 分支预测器           | 分支预测、感知机                  | ★★★☆☆   | `lab5`   | [查看](https://github.com/ren-yi-NUDT/CS-lab/blob/lab5/README.md)                                 |
| 6   | 性能优化实验         | CPE、循环展开、SIMD               | ★★★☆☆   | `lab6`   | [查看](https://github.com/ren-yi-NUDT/CS-lab/blob/lab6/README.md)                                 |
| 7   | Cache 模拟器         | 组相联、硬件预取、多级缓存        | ★★☆☆☆   | `lab7`   | [查看](https://github.com/ren-yi-NUDT/CS-lab/blob/lab7/README.md)                                 |
| 8   | 链接炸弹             | 符号解析、重定位                  | ★★★☆☆   | `lab8`   | [查看](https://github.com/ren-yi-NUDT/CS-lab/blob/lab8/README.md)                                 |
| 9   | Shell 实验           | 进程控制、fork/exec、信号         | ★★★☆☆   | `lab9`   | [查看](https://github.com/ren-yi-NUDT/CS-lab/blob/lab9/README.md)                                 |
| 10  | 动态内存分配器       | 分离空闲链表、边界标记            | ★★★★★   | `lab10`  | [查看](https://github.com/ren-yi-NUDT/CS-lab/blob/lab10/README.md)                                |
| 11  | Web 服务器实验       | socket、HTTP、fork 并发           | ★★★☆☆   | `lab11`  | [查看](https://github.com/ren-yi-NUDT/CS-lab/blob/lab11/readme.md)                                |
| 12  | 多线程计算实验       | pthread、AVX2、内嵌汇编           | ★★☆☆☆   | `lab12`  | [查看](https://github.com/ren-yi-NUDT/CS-lab/blob/lab12/README.md)                                |

---

## 🚀 快速开始

### 方法一：克隆整个仓库

```bash
git clone https://github.com/ren-yi-NUDT/CS-lab.git
cd CS-lab

# 切换到你想要查看的实验分支
git checkout lab10    # 例：查看 malloc 实验室
```

### 方法二：只克隆单个实验分支

仓库有 12 个分支，如果只想下载某一个实验，用 `--single-branch`：

```bash
# 只拉 lab10 分支（含历史），其他分支不下载
git clone -b lab10 --single-branch https://github.com/ren-yi-NUDT/CS-lab.git
cd CS-lab

# 想进一步省空间：浅克隆，只取最新一次提交
git clone -b lab10 --single-branch --depth 1 https://github.com/ren-yi-NUDT/CS-lab.git
```

后续如果又想看别的实验，在仓库目录里追加：

```bash
git fetch origin lab7                 # 把 lab7 拉下来
git checkout lab7                     # 切过去
```

### 方法三：在 GitHub 网页直接浏览

点击上方「在线 README」列的链接即可，无需克隆。

---

## 🗂️ 仓库结构

```
CS-lab/
├── inclass/  ← 当前分支（你在这里）
│   ├── README.md          # 项目总览（本文件）
│   ├── test.c             # 课上随手写的测试代码
│   └── fork-execve/       # fork + execve 示例（A/B/C 三个进程）
│
├── lab1/    ← 数据实验 · 位运算
├── lab2/    ← 二进制炸弹
├── lab3/    ← 缓冲区炸弹
├── lab4/    ← Y86-64 模拟器
├── lab5/    ← 分支预测器
├── lab6/    ← 性能优化
├── lab7/    ← Cache 模拟器
├── lab8/    ← 链接炸弹
├── lab9/    ← Shell 实验
├── lab10/   ← 动态内存分配器（malloc）
├── lab11/   ← Web 服务器
└── lab12/   ← 多线程计算
```

每个 `labN` 分支都包含：

- 完整源代码（含中文注释）
- `README.md` 实验说明
- `requirements.txt` 实验要求原文
- `helpme.md` 思路记录 / 踩坑笔记（部分实验）
- `report.md` 实验报告（部分实验）

---

## 🎓 学习路径建议

按由易到难、由底向上的顺序：

1. **数据级**（lab1–3）：理解数据在机器中的表示
2. **机器级**（lab4–5）：理解指令如何执行、预测
3. **体系结构**（lab6–7）：理解性能与缓存的权衡
4. **系统软件**（lab8–10）：理解链接、进程、内存管理
5. **并发与网络**（lab11–12）：理解网络服务与并行计算

---

## 📌 说明

- 本仓库为个人学习记录，参考教材为 *Computer Systems: A Programmer's Perspective* (CS:APP, 3rd Edition)
- 每个实验的 `requirements.txt` 是课程原文要求，请勿直接复制答案
- **如果派上用场，记得 star 哦** ⭐

---

## 📜 License

代码遵循课程要求，仅供学习参考。转载请注明出处。
