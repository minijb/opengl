---
name: dx-create
description: 在项目 dx/ 工作区创建新的 dx 知识单元，初始化 DESC.md、knowledge/reference/spec、tags/aliases/father，并重建索引。用户要求新增或创建 dx、主题、流程、规范、知识单元，或明确要求初始化某领域知识目录时使用；已有 dx 的修改、删除和索引用 dx-manage，查询已有知识用 dx-explore。
---

# dx-create

在 `<项目根>/dx/` 下创建一个新的 dx 知识单元（一个文件夹 = 一个主题/流程/规范）。

## 前置确认（先做，避免返工）

1. 若 `dx/INDEX.json` 不存在或与 `dx/` 实际内容明显不符，先运行 `python .agents/skills/dx-manage/scripts/reindex.py` 重建索引再继续。
2. 读 `dx/INDEX.json`：确认 id 未被占用；father 指向已存在的 id（根单元用 `null`）。
3. 用 `INDEX.json` 的 `lookup`（id/标题/别名 → dx）查重：lookup 命中、或用户口述主题与已有 dx 的标题/别名高度相似，即视为疑似重复；疑似重复时先向用户确认是扩展旧 dx 还是新建，用户不可达时保守不建并记录。
4. 与用户确认五要素：`id`（kebab-case 英文）、`title`（可中文）、`brief`（一句话，进索引）、`tags`、`father`；可选 `aliases`（用户常用说法列表，进 lookup）。某要素缺失且用户不可达时：`brief` 可代拟一句话（须在汇报中提示复核）；`father` 按硬性规则的保守缺省处理。
5. 判断是否需要 `spec/`：规范类 dx 才需要，可后补。

## 创建步骤

1. 创建目录 `dx/<id>/`。
2. 按本 skill 的 `templates/DESC.template.md` 写 `dx/<id>/DESC.md`（frontmatter 必填 `id/title/brief/father`）。
3. 创建 `knowledge/`、`reference/` 子目录（可为空；用户明确说不需要 reference 时可不建，`has` 字段会如实反映）；需要时创建 `spec/`。
4. 初始知识文件按 `templates/knowledge|reference|spec.template.md` 的 frontmatter 格式写；模板正文均为示例，按需改写，不必保留占位说明。
5. 运行索引脚本（在项目根执行）：
   ```bash
   python .agents/skills/dx-manage/scripts/reindex.py
   ```
   脚本会生成 `dx/INDEX.json` 与各子目录 `INDEX.json`，并校验引用。有 error 时按提示修复。
6. 若该 dx 有常用中文说法或别名，写进 DESC.md frontmatter 的 `aliases`（列表），reindex 后自动进入 `INDEX.json` 的 `lookup`；领域内部黑话较多时可在 `knowledge/terms.md` 里集中定义（可选）。
7. 向用户汇报：id、路径、父子关系；**与用户口述有任何偏差（代拟 brief、father 改用 null 等）时必须说明原因**。

## 硬性规则

- `id` = 文件夹名，kebab-case（全小写英文/数字/连字符），全工作区唯一；中文放 `title`。
- `father` 必须是已存在的 dx id；不存在时先创建父级，或经用户确认后用 `null`。**用户不可达且无法确认时，保守用 `null`，并在 DESC『与父级的关系』与汇报中标注待确认**。
- 子文件名用 kebab-case，一个主题一个文件；spec 一个 level 一个文件（frontmatter 只能存一个 level）。
- **禁止手写任何 INDEX.json**，一律由 reindex 脚本生成。
- 子目录只允许 `knowledge/`、`reference/`、`spec/`；dx 根下只放 `DESC.md`。
