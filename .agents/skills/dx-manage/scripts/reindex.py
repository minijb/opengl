#!/usr/bin/env python3
"""dx 索引重建与校验脚本。

用法:
    python reindex.py [--check] [dx目录]

默认自动定位 dx/ 目录（脚本位于 <根>/.agents/skills/dx-manage/scripts/）。
扫描所有 dx 单元的 DESC.md frontmatter 与子目录 md 文件，重建:
    dx/INDEX.json              —— 全部单元的总索引（含 children、tag 索引）
    dx/<id>/<sub>/INDEX.json   —— knowledge/reference/spec 的文件索引
DESC.md frontmatter 是唯一事实来源；本脚本输出均为生成物，禁止手改。
"""

import json
import re
import sys
from datetime import datetime, timezone
from pathlib import Path

SUBFOLDERS = ("knowledge", "reference", "spec")
SPEC_LEVELS = ("required", "default", "optional")
TOP_ALLOWED = {"INDEX.json"}


# ---------- frontmatter 解析（YAML 子集：key: value / [a, b] / 引号 / null / bool） ----------

def _split_top(s: str):
    parts, cur, depth, in_q = [], "", 0, None
    for ch in s:
        if in_q:
            cur += ch
            if ch == in_q:
                in_q = None
            continue
        if ch in "\"'":
            in_q = ch
            cur += ch
            continue
        if ch == "[":
            depth += 1
        elif ch == "]":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        parts.append(cur)
    return parts


def _scalar(raw: str):
    raw = raw.strip()
    if raw == "" or raw.lower() in ("null", "~"):
        return None
    if raw.lower() == "true":
        return True
    if raw.lower() == "false":
        return False
    if raw.startswith("[") and raw.endswith("]"):
        inner = raw[1:-1].strip()
        return [_scalar(p) for p in _split_top(inner)] if inner else []
    if len(raw) >= 2 and raw[0] == raw[-1] and raw[0] in "\"'":
        return raw[1:-1]
    return raw


def parse_frontmatter(text: str):
    """返回 (meta, body)。无合法 frontmatter 时 meta 为空 dict。"""
    if not text.startswith("---"):
        return {}, text
    lines = text.splitlines()
    end = None
    for i in range(1, len(lines)):
        if lines[i].strip() == "---":
            end = i
            break
    if end is None:
        return {}, text
    meta = {}
    for line in lines[1:end]:
        s = line.strip()
        if not s or s.startswith("#"):
            continue
        m = re.match(r"^([A-Za-z0-9_-]+)\s*:\s*(.*)$", s)
        if m:
            meta[m.group(1)] = _scalar(m.group(2))
    return meta, "\n".join(lines[end + 1:])


# ---------- 定位 dx 根 ----------

def find_dx_root(explicit):
    if explicit:
        p = Path(explicit).resolve()
        return p if p.is_dir() else None
    here = Path(__file__).resolve()
    for anc in here.parents:
        if (anc / "dx").is_dir() and (anc / ".agents").is_dir():
            return anc / "dx"
    cwd = Path.cwd().resolve()
    for anc in (cwd, *cwd.parents):
        if (anc / "dx").is_dir():
            return anc / "dx"
    return None


# ---------- 写入 ----------

def write_index(path: Path, data: dict) -> bool:
    """除 generated 时间戳外内容不变则不写，避免无意义的重写抖动。"""
    try:
        old = json.loads(path.read_text(encoding="utf-8"))
        strip = lambda d: {k: v for k, v in d.items() if k != "generated"}
        if strip(old) == strip(data):
            return False
    except (OSError, json.JSONDecodeError):
        pass
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return True


# ---------- 收集 ----------

def build_items(folder: Path, sub: str, rel, warnings):
    items = []
    for f in sorted(folder.glob("*.md")):
        meta, _ = parse_frontmatter(f.read_text(encoding="utf-8", errors="replace"))
        title, brief = meta.get("title"), meta.get("brief")
        if title is None:
            warnings.append(f"{rel(f)}: frontmatter 缺少 title")
        if brief is None:
            warnings.append(f"{rel(f)}: frontmatter 缺少 brief")
        item = {
            "file": f.name,
            "title": title if isinstance(title, str) else str(title or ""),
            "brief": brief if isinstance(brief, str) else str(brief or ""),
        }
        tags = meta.get("tags")
        if isinstance(tags, list):
            item["tags"] = [str(t) for t in tags]
        if sub == "reference":
            if meta.get("source"):
                item["source"] = str(meta["source"])
            if meta.get("fetched"):
                item["fetched"] = str(meta["fetched"])
        if sub == "spec":
            level = meta.get("level", "default")
            level = str(level) if level is not None else "default"
            if level not in SPEC_LEVELS:
                warnings.append(f"{rel(f)}: level '{level}' 无效，已按 default 兜底（应为 required/default/optional）")
                level = "default"
            item["level"] = level
        items.append(item)
    return items


def main(argv):
    check = "--check" in argv
    positional = [a for a in argv if not a.startswith("--")]
    dx_root = find_dx_root(positional[0] if positional else None)
    if dx_root is None:
        print("[E] 未找到 dx/ 目录，请显式传入路径", file=sys.stderr)
        return 1

    def rel(p: Path) -> str:
        return str(p.relative_to(dx_root)).replace("\\", "/")

    errors, warnings, units = [], [], {}

    for d in sorted(p for p in dx_root.iterdir() if p.is_dir() and not p.name.startswith(".")):
        desc = d / "DESC.md"
        if not desc.is_file():
            if not any(p.is_file() for p in d.rglob("*")):
                warnings.append(f"{rel(d)}: 空壳目录（无 DESC.md 也无任何文件），已忽略；可删除")
            else:
                errors.append(f"{rel(d)}: 缺少 DESC.md，不是有效 dx")
            continue
        meta, _ = parse_frontmatter(desc.read_text(encoding="utf-8", errors="replace"))
        uid = d.name
        if meta.get("id") is None:
            warnings.append(f"{rel(desc)}: frontmatter 缺少 id，按文件夹名处理")
        elif meta["id"] != uid:
            errors.append(f"{rel(desc)}: id '{meta['id']}' 与文件夹名 '{uid}' 不一致")
        if uid in units:
            errors.append(f"dx id 重复: {uid}")
            continue

        father = meta.get("father")
        if father is not None and not isinstance(father, str):
            errors.append(f"{rel(desc)}: father 应为字符串 id 或 null")
            father = None
        tags = meta.get("tags")
        if tags is None:
            tags = []
        elif not isinstance(tags, list):
            warnings.append(f"{rel(desc)}: tags 应为列表，已忽略")
            tags = []
        aliases = meta.get("aliases")
        if aliases is None:
            aliases = []
        elif not isinstance(aliases, list):
            warnings.append(f"{rel(desc)}: aliases 应为列表，已忽略")
            aliases = []
        if meta.get("title") is None:
            warnings.append(f"{rel(desc)}: frontmatter 缺少 title")
        if meta.get("brief") is None:
            warnings.append(f"{rel(desc)}: frontmatter 缺少 brief")

        has, files, sub_items = {}, {}, {}
        for child in sorted(d.iterdir()):
            if child.is_dir():
                if child.name in SUBFOLDERS:
                    items = build_items(child, child.name, rel, warnings)
                    sub_items[child.name] = items
                    has[child.name] = True
                    files[child.name] = len(items)
                else:
                    warnings.append(f"{rel(child)}: dx 内只允许 knowledge/reference/spec 子目录")
            elif child.name != "DESC.md":
                warnings.append(f"{rel(child)}: dx 根下只应有 DESC.md 与子目录")

        units[uid] = {
            "id": uid,
            "title": meta.get("title") if isinstance(meta.get("title"), str) else str(meta.get("title") or ""),
            "brief": meta.get("brief") if isinstance(meta.get("brief"), str) else str(meta.get("brief") or ""),
            "tags": [str(t) for t in tags],
            "aliases": [str(a) for a in aliases],
            "father": father,
            "children": [],
            "has": has,
            "files": files,
            "_dir": d,
            "_sub_items": sub_items,
        }

    # alias 与其他 dx 的 id 重名时提醒（易混淆路由）
    for uid, u in units.items():
        for a in u["aliases"]:
            if a in units and a != uid:
                warnings.append(f"{uid}: alias '{a}' 与其他 dx 的 id 重名，易混淆")

    # father 引用 / 环 校验
    for uid, u in units.items():
        f = u["father"]
        if f is None:
            continue
        if f == uid:
            errors.append(f"{uid}: father 不能指向自己")
        elif f not in units:
            errors.append(f"{uid}: father '{f}' 不存在")
    for uid in units:
        seen, cur = set(), uid
        while cur is not None and cur in units:
            if cur in seen:
                errors.append(f"{uid}: father 链成环")
                break
            seen.add(cur)
            cur = units[cur]["father"]

    # children / tag 索引
    tag_index = {}
    for uid, u in sorted(units.items()):
        f = u["father"]
        if f is not None and f in units:
            units[f]["children"].append(uid)
        for t in u["tags"]:
            tag_index.setdefault(t, []).append(uid)

    # lookup：id / 标题 / 别名 → dx id（术语路由，取代独立术语表）
    lookup = {}
    for uid, u in sorted(units.items()):
        for key in [u["id"], u["title"], *u["aliases"]]:
            if isinstance(key, str) and key and uid not in lookup.setdefault(key, []):
                lookup[key].append(uid)
    lookup = {k: sorted(v) for k, v in sorted(lookup.items())}

    for p in dx_root.iterdir():
        if p.is_file() and p.name not in TOP_ALLOWED:
            warnings.append(f"{rel(p)}: dx/ 顶层只应有 INDEX.json")

    # 报告
    print(f"dx root: {dx_root}")
    print(f"units: {len(units)}"
          + ("".join(f"\n  - {u['id']}: father={u['father']} children={u['children'] or '[]'}"
                     f" tags={u['tags']} files={u['files'] or '{}'}"
                     for _, u in sorted(units.items()))))
    for w in warnings:
        print(f"[W] {w}")
    for e in errors:
        print(f"[E] {e}")
    print(f"result: {len(units)} units, {len(warnings)} warnings, {len(errors)} errors"
          + ("  (--check，未写文件)" if check else ""))

    if errors:
        print("[E] 存在校验错误，已跳过写入；请修复后重跑", file=sys.stderr)
        return 1
    if check:
        return 0

    generated = datetime.now(timezone.utc).astimezone().isoformat(timespec="seconds")
    written = []

    top = {
        "version": 1,
        "generated": generated,
        "units": [{k: v for k, v in u.items() if not k.startswith("_")}
                  for _, u in sorted(units.items())],
        "tags": {t: sorted(ids) for t, ids in sorted(tag_index.items())},
        "lookup": lookup,
    }
    if write_index(dx_root / "INDEX.json", top):
        written.append("INDEX.json")

    for uid, u in sorted(units.items()):
        for sub, items in u["_sub_items"].items():
            sub_index = {"folder": sub, "generated": generated, "items": items}
            target = u["_dir"] / sub / "INDEX.json"
            if write_index(target, sub_index):
                written.append(rel(target))

    if written:
        print("written:")
        for w in written:
            print(f"  {w}")
    else:
        print("written: 无变化")
    return 0


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    sys.exit(main(sys.argv[1:]))
