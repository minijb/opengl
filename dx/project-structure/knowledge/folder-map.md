---
title: 全部目录一览表（角色 / 参与构建? / git 跟踪?）
brief: 每个文件夹的作用与状态,按"代码/资产/学习记录/构建产物/版本控制与工具"分组。
tags: [structure, directory]
---
# 全部目录一览表

## 代码（参与构建）

| 目录 | 作用 | 状态 |
|---|---|---|
| `src/` | 编译进 CG_Lab.exe 的源文件,由 CMake `add_executable` **显式列出**:main.cpp(当前课程主程序,每课重写)、tools.cpp(ReadFile)、window.cpp(GLFW RAII 窗口)、stb_impl.cpp(stb 唯一实现 TU) | 构建 ✓ / git ✓ |
| `include/` | header-only 封装库:GLObject CRTP 家族(gl_object/gl_buffer/gl_vertex_array/gl_program/gl_mesh/gl_texture) + 旧风格自由函数(gl_common/texture_common) + window.hpp/tools.hpp + stb_image.h(第三方单头) + m_sharder.hpp(骨架) | 构建 ✓(仅被 include) / git ✓ |

## 资产（CMake 构建后复制到 exe 旁，按 LearnOpenGL 章节组织）

| 目录 | 作用 | 状态 |
|---|---|---|
| `shader/` | GLSL 着色器,子目录 = `1.getting_started/<章节号.章节名>/`;代码经 `SHADER_DIR` 宏访问构建目录里的副本 | 构建 ✓(复制) / git ✓ |
| `texture/` | 贴图(container.jpg、awesomeface.png、checker.ppm 等),`TEXTURE_DIR` 宏同上 | 构建 ✓(复制) / git ✓ |
| `model/` | 3D 模型资产,预留(.gitkeep 占位),课程到模型加载章再启用,`MODEL_DIR` 宏已配好 | 构建 ✓(复制) / git ✓(空) |

## 学习记录与文档（不参与构建）

| 目录 | 作用 | 状态 |
|---|---|---|
| `place/` | 学习记录存档:每课推进后被替换的上一版 main.cpp 原样快照,**只增不改**(git 负责"改",这里负责"留") | 构建 ✗ / git ✓ |
| `docs/` | 调研报告:graphics_api_data_structures.md(图形 API 数据结构 8 项目取证)、modern_cpp_idioms.md([[nodiscard]]/CRTP 等) | 构建 ✗ / git ✓ |
| `temp/` | 实验区:一次性工具与验证脚本(verify_screenshot.py 等),不进构建 | 构建 ✗ / git ✓ |
| `dx/` | 知识库工作区(本体系):每个 dx = 一个文件夹(DESC.md + knowledge/reference/spec + 脚本生成的 INDEX.json) | 构建 ✗ / git ✓ |

## 构建产物（gitignore,不提交）

| 目录 | 作用 | 状态 |
|---|---|---|
| `out/` | CMake 构建树:out/build/windows-msvc/Debug/CG_Lab.exe + 同步过去的 shader/texture/model 副本 | 构建产物 / git ✗ |
| `vcpkg_installed/` | vcpkg 依赖安装产物(glad/glfw/glm,x64-windows) | 构建产物 / git ✗ |

## 版本控制与工具（隐藏目录）

| 目录 | 作用 | 状态 |
|---|---|---|
| `.git/` | git 仓库本体 | git 本体 |
| `.jj/` | jujutsu VCS 共置状态(VisualJJ 扩展管理;`git status` 显示 detached HEAD 属正常);提交用 `jj commit` + `jj git push` | git ✗(ignore) |
| `.vscode/` | 编辑器配置(settings.json) | git ✓ |
| `.agents/` | dx 技能定义(dx-create/dx-manage/dx-explore + scripts/reindex.py) | git ✓ |
| `.pi/` | pi coding agent 的会话/缓存数据 | git ✗(ignore) |

## 根文件速记

| 文件 | 作用 |
|---|---|
| `CMakeLists.txt` | CG_Lab 目定义:源文件列表、include/、SHADER_DIR 等 5 个宏、资产目录自动复制(见 asset-dir-wiring) |
| `CMakePresets.json` | windows-msvc 预设:VS2022 x64 + vcpkg toolchain(VCPKG_ROOT 环境变量) |
| `vcpkg.json` | 依赖清单(glad/glfw/glm) |
| `README.md` | vcpkg 环境变量配置说明 |
