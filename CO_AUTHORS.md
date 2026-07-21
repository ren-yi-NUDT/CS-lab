# AI 协作声明 · AI Co-Authors

本仓库的部分代码、文档、commit message 由以下 AI 工具协助生成。
This repository was developed with the assistance of the following AI tools.

## 工具与模型 · Tools & Models

| 角色 | 名称 | 提供方 | 用途 |
| --- | --- | --- | --- |
| 平台 | [Claude Code](https://www.anthropic.com/claude-code) | Anthropic | CLI 开发平台、工具链、agent 编排 |
| 模型 | [GLM-5.2](https://z.ai) | Z.ai / 智谱AI | 主力对话与编码模型 |
| 模型 | [DeepSeek](https://www.deepseek.com) | 深度求索 | 部分代码与文档生成 |

> Claude Code 是 Anthropic 提供的 CLI 开发平台，本仓库通过该平台调度其他模型完成具体任务。

## commit 标注约定 · Commit Trailer Convention

涉及 AI 协作的 commit，使用标准 `Co-Authored-By` trailer 标注：

```
Co-Authored-By: Claude Code <noreply@anthropic.com>
Co-Authored-By: GLM <noreply@z.ai>
Co-Authored-By: DeepSeek <noreply@deepseek.com>
```

如某次 commit 仅由特定模型完成，可只标注对应的 `Co-Authored-By` 行，并在 commit message 正文说明。
