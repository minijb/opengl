# Jujutsu (jj) 简明使用教程

> 取证来源:jj 官方文档 [tutorial](https://martinvonz.github.io/jj/latest/tutorial/)、
> [git-comparison](https://martinvonz.github.io/jj/latest/git-comparison/)、
> [git-command-table](https://martinvonz.github.io/jj/latest/git-command-table/)(master 分支源文件),
> 以及**本仓库真实输出**(jj 0.47.1+visualjj,VS Code VisualJJ 扩展管理)。
> 面向读者:会 git、在共置仓库里日常要动手的人。

## 0. jj 是什么,为什么本仓库有 `.jj/`

jj 是一个 Git 兼容的新一代版本控制系统(Rust 写的,作者 Martin von Zweigbergk,
曾是 Mercurial 核心开发者)。核心卖点:**操作历史可撤销、冲突可以提交、工作副本自动提交**。

本仓库是 **jj + git 共置(colocated)仓库**:`.git/`(git 仓库本体)和 `.jj/`(jj 状态)
并排存在——因为 VS Code 装了 VisualJJ 扩展,它对已有的 git 仓库执行过 `jj git init`,
从此 `jj` 和 `git` 命令**在同一目录都能用**,推的是同一个 origin。
git 只读命令(`git log` / `git show` / `git diff`)随时可以随便用;**写历史的操作交给 jj**(见 §4 的坑)。

## 1. 心智模型:与 git 的三个核心差异

① **工作副本本身就是一个提交**。jj 里没有 staging area、没有 `git add`:
你每次运行任何 jj 命令,它先把当前工作副本**快照**进"工作副本提交"(用 `@` 表示),
然后才执行命令。改了文件不运行 jj 命令,快照就不会发生。

② **change ID 与 commit ID 分离**。每个提交有两套 ID:

```
@  rrqmtuvp zhouh7150@163.com 2026-11-27 00:22:59 b661fa03
   ↑ change ID(稳定)                       ↑ commit ID(会变)
```

改描述、改文件、rebase 都会让 commit ID(哈希)变化,但 **change ID 永远不变**
(类似 Gerrit 的 Change-Id)。给命令传修订号时优先用 change ID。

③ **没有"当前分支",HEAD 永远 detached**。jj 里 checkout 任何一个提交都是"detached"
状态,新提交不会自动推进任何分支——分支改叫 **bookmark(书签)**,由你手动推进
(本仓库 `git status` 显示 `HEAD (no branch)` 就是这个原因,**不是坏了**)。

其他值得知道的差异(用到再细究):冲突可以提交进历史再慢慢解、重写提交时后代自动
rebase、reflog 升级成了全局"操作日志"(`jj op log`)并支持一键 `jj undo`。

## 2. 日常命令对照表(git → jj)

| 场景 | git | jj |
|---|---|---|
| 看状态 | `git status` | `jj status` / `jj st` |
| 看工作副本改动 | `git diff HEAD` | `jj diff` |
| 看提交历史 | `git log` | `jj log`(`@` = 工作副本,`◆` = 远端/不可变) |
| 提交并继续新的 | `git commit -m "..."` | `jj commit -m "..."` |
| 只改提交信息 | `git commit --amend`(危险) | `jj describe -m "..."`(家常便饭,可反复改) |
| 改动并进上一提交 | `git commit --amend` | `jj squash`(把 @ 的改动并进父提交) |
| 部分文件提交 | `git add <file> && git commit` | `jj split <file>`(把 @ 拆成两个提交) |
| 交互式挑 hunk | `git add -p` | `jj split -i` / `jj squash -i` |
| 新开一个工作提交 | `git checkout -b` | `jj new`(在 @ 之上开新空提交) |
| 丢弃一个提交 | `git rebase -i` 删行 | `jj abandon <change ID>`(后代自动接上) |
| 回滚刚才的操作 | reflog 大法 | `jj undo`(操作级回滚,含工作副本) |
| 换分支 | `git checkout main` | `jj new main` / `jj edit main` |
| 分支 | `git branch` | `jj bookmark list`(set / move / track) |
| 拉取 | `git fetch` | `jj git fetch` |
| 推送 | `git push` | `jj git push` |
| 看操作历史 | `git reflog` | `jj op log` |

## 3. 本仓库(共置)的标准工作流 —— 实战代码

上一课推进时真实用过的四步,以后照抄即可:

```bash
# ① 看状态:改动已经被(VisualJJ 或你敲过的)jj 命令快照成 @ 了
jj st

# ② 定稿:给 @ 写描述,并在其上生成新的空 @(= git commit)
jj commit -m "feat: xxx"

# ③ 推进 main 书签到新提交(= git 里 main 指针前移)
jj bookmark set main -r @-

# ④ 推送(= git push origin main)
jj git push
```

拉取远端并查看差异:`jj git fetch` 然后 `jj log`(远端提交显示为 `◆`)。

替代风格:`jj describe -m "..."`(只写描述、@ 保持是 tip)→
`jj bookmark set main -r @` → `jj git push`,效果一样,看你习惯。

## 4. 坑:为什么这个仓库不要裸用 `git commit`

jj 每次运行都会重置 git index(所以本仓库 `git diff --cached` 永远是空的、
`git status` 显示的都是"未暂存")。此时裸 `git add + git commit` 会:

- 得到一个**内容与 jj 快照提交相同、但哈希不同**的重复提交;
- jj 侧出现分叉,VisualJJ 的时间线视图变得混乱,要手动 abandon 清理。

**规则:写历史(commit/rebase/reset/bookmark)用 jj;只读(git log/show/diff/blame)随便用 git。**
万一已经裸 commit 了也别慌:`jj st` 看状态,重复的旧快照用 `jj abandon <change ID>` 清掉。

## 5. 改错了怎么办(救场手册)

- `jj undo` —— 撤销上一条 jj 操作(连工作副本状态一起恢复,操作日志级回滚,
  比 git 的 reflog + reset --hard 简单一个量级);
- `jj op log` —— 看操作历史;`jj log --at-op <op ID>` 回看某次操作后的仓库样子;
- `jj abandon <change>` —— 丢掉某个提交,后代自动 rebase 到它的父提交上;
- `jj split` / `jj split <文件>` —— 把 @ 拆成两个提交(代替 `git add -p; git commit`);
- `jj squash` —— 把 @ 的改动并进父提交(`--amend` 的手感),`-i` 可交互挑 hunk;
- `jj new` —— "这个提交到此为止,开下一个":当前 @ 定稿,新空 @ 落在其上。

## 6. VS Code / VisualJJ 用户须知

- 扩展会**自动快照**(编辑器里 `jj op log` 常见成串的 `snapshot working copy`
  记录,就是 VisualJJ 干的),命令行 jj 与扩展共用同一份 `.jj/` 状态,互不打架;
- VisualJJ 的源代码管理面板有图形化时间线(改描述、abandon、split 都能点);
- 命令行 `jj` 二进制由扩展自带(本仓库为 0.47.1+visualjj,
  路径在 `.vscode/extensions/visualjj.*/dist/bin/`),无需单独安装。

## 7. 参考链接

- 官方教程:https://martinvonz.github.io/jj/latest/tutorial/
- 与 git 的概念对比:https://martinvonz.github.io/jj/latest/git-comparison/
- git↔jj 命令对照表:https://martinvonz.github.io/jj/latest/git-command-table/
- 共置仓库说明:https://martinvonz.github.io/jj/latest/git-compatibility/
- 第三方教程:Steve Klabnik 的 [jj tutorial](https://steveklabnik.github.io/jujutsu-tutorial/)、
  [Jujutsu for everyone](https://jj-for-everyone.github.io)
