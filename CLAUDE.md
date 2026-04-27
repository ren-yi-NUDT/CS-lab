# Lab5 - Branch Predictor

## 项目结构

- `predictor.c` — 分支预测器实现，所有实验代码都在这个文件里
- `run_all.sh` — 并行测试脚本，自动 make clean + 编译 + 跑所有 trace
- `Result.xlsx` — 实验结果表格（9 种预测方法 × 13 个 trace）
- `Readme.md` — 项目文档（实验设计思路 + 自动化流程）

## predictor.c 工作流

文件中的实验代码按倒序排列（最新实验在顶部），每个实验之间用注释块分隔：

```
// ==================== 实验N: 方法名 ====================
// 活跃代码（当前生效）

// ==================== 实验8: 方法名 ====================
// 已注释的代码
// 结果已写入 Result.xlsx Row 8

// ==================== 实验7: ... ====================
// ...
```

- 每次新实验：把当前活跃代码整块注释掉，在上方写新代码
- 公共的 `#include`、`#define TAKEN/NOT_TAKEN` 在最顶部不注释
- `#define` 如果不同实验有冲突，需要放在各自的实验块里

## 实验列表（对应 Result.xlsx 行号）

| Row | 方法 | 状态 |
|-----|------|------|
| 2 | static (总是预测Taken) | 已完成 |
| 3 | 2bits PC>>2 | 已完成 |
| 4 | local PC>>2 | 已完成 |
| 5 | gshare PC>>2 | 已完成 |
| 6 | gshare PC>>1 | 已完成 |
| 7 | gshare (PC XOR ghr, 17位) | 已完成 |
| 8 | gshare PC<<1 | 已完成 |
| 9 | global (10位全局历史) | 已完成 |
| 10 | local (3位局部历史+10位PC) | 已完成 |

## 测试与记录流程

1. 用户修改 predictor.c，写新实验代码
2. 运行 `./run_all.sh`（自动 make clean + 编译 + 并行测试 13 个 trace）
3. 脚本输出每个 trace 完整结果 + MISPRED_PER_1K_INST 汇总表
4. 将结果写入 Result.xlsx 对应行（C-O 列，列顺序与 trace 顺序一致）
