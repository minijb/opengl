---
title: 代码落位规则（src / include / place / temp 怎么选）
brief: 新写一个 .cpp/.hpp 时该放哪个目录,含 stb_impl.cpp 这个唯一例外案例。
tags: [structure, code-organization]
---
# 代码落位规则

## 决策表

| 写的是什么 | 放哪 | 原因 |
|---|---|---|
| GL 封装类/工具的**声明与内联实现** | `include/` | 本项目封装库是 header-only 风格,GLObject CRTP 家族全在头文件里 |
| 需要**独立编译单元**的实现 | `src/` | 并入 `add_executable` 列表;目前只有 main/tools/window/stb_impl 四个 |
| 每课重写后的**旧版 main.cpp** | `place/` | 学习记录,只增不改,不进构建 |
| **一次性**实验/验证脚本 | `temp/` | 仓库文化的实验区(曾有过 temp/build),git 跟踪但不进构建 |
| 第三方单头库 | `include/` | 如 stb_image.h |

## 关键规则

1. **CMake 只编译 `src/` 里显式列出的文件**——放错目录不会报错,只会"悄悄不被编译",症状是链接错误或改了没效果。
2. 新增 `src/` 文件后必须手动加进 `CMakeLists.txt` 的 `add_executable(CG_Lab ...)`,忘了就是链接失败。
3. `include/` 里的头通过 `target_include_directories(CG_Lab PUBLIC include)` 已全局可见,include 时写 `"gl_xxx.hpp"` 即可,无需路径前缀。

## 例外案例:stb_impl.cpp 为什么在 src/ 而不是 include/

stb_image.h 是"声明与实现一体"的单头库:直接 include 只拿声明,必须在**恰好一个** TU 里先 `#define STB_IMAGE_IMPLEMENTATION` 再 include 才生成函数体;两处都定义 = ODR 重复符号。所以它虽然是"第三方库的一部分",却必须是 `src/` 里的独立编译单元(历史教训:texture_common.hpp 曾长期没有实现 TU,loadTexture 一旦被调用就链接失败)。
