---
name: dx-explore
description: 以渐进披露方式检索和阅读项目 dx/ 知识库，通过 INDEX.json 的 units/tags/lookup、DESC.md 和子目录索引定位知识，并区分 knowledge/reference/spec 的信任与执行级别。用户询问工作区已有 dx、主题知识、术语别名、流程或规范，或需要引用知识来源时使用；本 skill 只读，创建用 dx-create，修改删除用 dx-manage。
---

# dx-explore

按渐进披露原则读取 dx 知识库，为当前任务填充上下文。**只读需要的，不要一次读入全部内容。**

## 读取顺序

1. `dx/INDEX.json` —— 所有 dx 的 `id/tags/father/children/brief`。按 tag、brief、父子树定位候选。
2. 用户用词含糊 → 用 `dx/INDEX.json` 的 `lookup`（id/标题/别名 → dx）对齐指向；领域黑话词（如 NIT）本身不在 lookup 里是设计内的——先路由到所属域 dx，再查它的 `knowledge/terms.md`（若存在）。
3. 读候选 dx 的 `DESC.md`：是什么 / 作用 / 与父级关系。仍不确定时换候选或同时读多个 DESC（都很短）。
4. **沿 father 链补齐上下文**：定位到的 dx 若有 `father`，同时读父 dx 的 `DESC.md`（逐级向上，都很短），理解「父管什么、子管什么」与规范归属。仅当本次任务会产出内容（写教程/回写知识）时，再按需读父 dx 相关的 `spec/`；不主动全量载入父级知识文件。
5. 需要细节时，读对应子目录的 `INDEX.json`（每条一句话 brief）→ **最后**才读具体文件。

## 信任级别（写进上下文/结论时必须区分）

- `knowledge/`：可信的内部知识，可直接引用。
- `reference/`：不可信的外部知识，只作线索；引用前核实，注明其 `source` 与 `fetched` 日期。
- `spec/`：分级操作规范，按 `level` 执行：
  - `required`：必须遵守
  - `default`：默认执行，偏离需说明理由
  - `optional`：按情形自由采用

## 引用格式

回答中引用知识时标注来源，如 `(dx:dx-system/knowledge/frontmatter.md)`（示例，实际按所引文件替换），方便用户回查。

## 边界

- 本 skill 只读不写。需要创建/修改/删除 → 用 dx-create / dx-manage。
- 找不到合适 dx 时如实告知用户，不要编造；可建议创建新 dx。
