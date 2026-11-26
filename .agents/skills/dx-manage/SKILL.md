---
name: dx-manage
description: 修改、重命名、移动或删除项目 dx/ 中的知识单元及其知识文件，维护父子关系、tags/aliases，并通过 scripts/reindex.py 重建、校验或修复 INDEX.json。用户要求编辑已有 dx、调整 father、增删知识文件、删除 dx、重建或检查索引时使用；新建 dx 用 dx-create，只读查询用 dx-explore。
---

# dx-manage

修改、删除、重命名 dx 知识单元或其中的知识文件，并维护 `INDEX.json` 索引。

## 原则

- **DESC.md frontmatter 是元数据唯一事实来源；INDEX.json 是生成物，禁止手改。**
- 任何改动完成后，在项目根运行：
  ```bash
  python .agents/skills/dx-manage/scripts/reindex.py
  ```
- 只校验不写文件：`python .agents/skills/dx-manage/scripts/reindex.py --check`
- 脚本报 error 时按提示修复 DESC.md 或文件结构，再重跑。
- 多步相关改动可合并跑一次 reindex（每步都跑无害但冗余）。

## 修改元数据

- 改 `title/brief/tags/aliases` 或描述正文：直接编辑 `dx/<id>/DESC.md`。
- 改 `father`：新父必须已存在（查 `dx/INDEX.json`）；children 由脚本自动重算；**DESC 正文「与父级的关系」段必须同步改写**（frontmatter 与正文不能自相矛盾）。
- 新增子文件：必须是带 frontmatter 的 .md（参照 `dx-create/templates/`）：
  - knowledge 文件：必填 `title/brief`
  - reference 文件：另加 `source`（原始 URL）、`fetched`（日期）——可选但强烈建议
  - spec 文件：另加 `level`（required/default/optional）
- 删除子文件：直接删，然后 reindex。

## 重命名 / 移动

1. 重命名文件夹（保持 kebab-case），同步修改 DESC.md 的 `id`。
2. 全局更新引用（检索提示：`grep -rn "dx:<old-id>" dx/ .agents/`，另查所有 DESC.md 的 `father` 字段）。
3. reindex。

## 删除

0. 删除前先跑 `reindex --check`（或直接 reindex）确认索引新鲜——children 判断必须基于与文件系统一致的索引，不能信任可能漂移的旧 INDEX.json。
1. **有子 dx（INDEX 中 children 非空）时拒绝删除**；向用户给出选项：子级改挂其他 dx / 改为根（null）/ 连同子级一起删，确认后先改子级 father 再删。
2. 删除前向用户列出影响面：将删除的文件数、涉及的知识内容（`dx/` 未入 git 时删除不可恢复）。
3. 删除整个 `dx/<id>/` 文件夹后 reindex（别名与索引条目随 DESC 自动消失，无需手工清理）。

## 索引修复

- 怀疑索引漂移（文件与 INDEX 不一致）→ 直接 reindex，一切按文件系统与 DESC 重建。
- 校验范围：id 与文件夹名一致、father 引用存在、father 链无环、id 无重名、frontmatter 必填字段。
