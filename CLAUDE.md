# Lab5 - Branch Predictor

## 项目结构

- `predictor.c` — 分支预测器实现，所有实验代码都在这个文件里
- `run_all.sh` — 并行测试脚本，自动 make clean + 编译 + 跑所有 trace
- `Result.xlsx` — 实验结果表格（10种预测方法 × 13个trace）

## predictor.c 工作流

文件中的实验代码按顺序排列，每个实验之间用注释块分隔：

```
// ==================== 实验N: 方法名 ====================
// 活跃代码（当前生效）

// ==================== 实验N-1: 方法名 ====================
// 已注释的代码

// ==================== 实验1: ... ====================
// ...
```

- 每次新实验：把当前活跃代码整块注释掉，在上方写新代码
- 公共的 `#include`、`#define TAKEN/NOT_TAKEN` 在最顶部不注释
- `#define` 如果不同实验有冲突，需要放在各自的实验块里

## 实验列表（对应 Result.xlsx 行号）

| Row | 方法 | 状态 |
|-----|------|------|
| 2 | static (总是预测Taken) | 已完成 |
| 3 | 2bits PC>>2 | |
| 4 | local PC>>2 | |
| 5 | gshare PC>>2 | |
| 6 | gshare PC>>1 | |
| 7 | gshare (PC XOR ghr, 17位) | 已完成 |
| 8 | gshare PC<<1 | |
| 9 | global (10位全局历史) | |
| 10 | local (3位局部历史+10位PC) | |

## 测试与记录流程

1. 用户修改 predictor.c，写新实验代码
2. 运行 `./run_all.sh`（自动 make clean + 编译 + 并行测试13个trace）
3. 脚本输出每个trace完整结果 + MISPRED_PER_1K_INST 汇总表
4. 将结果写入 Result.xlsx 对应行（C-O列，列顺序与trace顺序一致）
