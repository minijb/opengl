# place/ —— 学习记录存档

每推进一课、`src/main.cpp` 被改写成新章节时，**上一版原样存档到这里**。
这些 `.cpp` 不参与构建（CMake 只编译 `src/` 里显式列出的文件），纯粹是
"这门课我走到哪了、每一步封装是怎么长出来的"的实物记录。

## 存档一览

| 文件 | 课程章节 | 这一版学到什么 |
|---|---|---|
| `main_3.3.shaders_class.cpp` | 1.getting_started/3.3 | 首次用手写流程换装 CRTP/RAII：`Program::fromFiles` + `Mesh` 属性表 + `setVec3`（自动 use + location 缓存），末尾 `glDeleteXXX` 全部消失 |

## 版本链

```
手写版(git 历史 40ba011:src/main.cpp)
  └─ place/main_3.3.shaders_class.cpp   ← 裸 GL → GLObject<CRTP> 封装
      └─ src/main.cpp(当前)= 4.2.textures_combined
            ← Texture2D 首次登场: fromFile + 双纹理单元 + 索引 Mesh(EBO)
```

## 约定

- 文件名 = `main_<章节号>.<章节名>.cpp`，与 `shader/` 目录的章节命名一致；
- 存档只增不改：后来发现旧代码有问题，也不回头改这里的文件（git 负责"改"，这里负责"留"）。
