---
title: 资产目录接线（shader/texture/model 的复制机制与新增步骤）
brief: CMake 如何把资产目录同步到 exe 旁,以及新增一个资产目录需要改哪三处。
tags: [structure, cmake, assets]
---
# 资产目录接线

## 机制:为什么代码里能按相对路径读到资产

exe 生成在 `out/build/windows-msvc/Debug/`,而资产在仓库源目录里——直接相对路径读不到。CMake 的解法:

1. **编译期**:`target_compile_definitions` 定义 `SHADER_DIR="$<TARGET_FILE_DIR:CG_Lab>/shader"` 等宏(生成器表达式,自动适配 Debug/Release);
2. **链接后**:`foreach` 遍历 `CG_LAB_ASSET_DIRS`(shader/texture/model),每个目录一条 `POST_BUILD` 自定义命令,`copy_directory` 把源目录同步到 exe 同级;
3. **代码里**:`#define SHADER(rel) SHADER_DIR "/" rel` 再 `SHADER("1.getting_started/.../xxx.vs")`。

结果:exe 旁边的资产永远与仓库同步(每次构建成功后重拷),代码不关心工作目录在哪。

## 新增一个资产目录(如 `font/`)的三步

1. 仓库根建目录(空目录需 `.gitkeep` 占位),内部按 LearnOpenGL 章节习惯再分子目录;
2. `CMakeLists.txt` 的 `target_compile_definitions(CG_Lab PRIVATE ...)` 里加一行:
   `FONT_DIR="$<TARGET_FILE_DIR:CG_Lab>/font"`
3. 同文件的 `set(CG_LAB_ASSET_DIRS shader texture model)` 列表里追加 `font`;
4. (代码侧)main.cpp 里加 `#define FONT(rel) FONT_DIR "/" rel` 使用。

漏掉第 2 步 → 编译错"FONT_DIR 未定义";漏掉第 3 步 → 运行时"文件打开失败"且看不出原因(路径存在但没内容被拷过去)。

## 现有宏一览

| 宏 | 指向 | 使用约定 |
|---|---|---|
| `SHADER_DIR` | exe 同级 `shader/` | `SHADER(rel)` |
| `TEXTURE_DIR` | exe 同级 `texture/` | `TEXTURE(rel)` |
| `MODEL_DIR` | exe 同级 `model/` | `MODEL(rel)`(预留) |

## 资产子目录命名

沿用 shader/ 的课程章节结构:`<大章>/<小节号.小节名>/`(如 `texture/1.getting_started/`),与 LearnOpenGL 仓库的目录树对应,方便对照原版资源。
