# OpenGL 游戏引擎：窗口管理器从零到精通

> 一套围绕 `GLFWManager` 展开的实战教程，带你从一个把 `glfwInit` 堆在 `main` 里的脚本式 demo，
> 一步步重构出一个可扩展、可替换、可测试的引擎级窗口子系统。

---

## 这套教程是给谁的？

- 看得懂 C++ 基础语法（类、指针、`std::function`、智能指针）。
- 能跑通最简单的 GLFW + GLEW 红屏窗口，但觉得代码越写越乱。
- 想学「真正的游戏引擎是怎么组织代码的」，而不只是堆 OpenGL API。
- 喜欢动手：每篇都带可直接复制进项目的代码。

如果你还从没跑通过任何 OpenGL 窗口，建议先花一小时把本仓库 `src/main.cpp` 现在的样子跑起来（红屏窗口），再回来。

---

## 学习路线图

**基础篇与进阶篇各自独立编号**（基础篇 01–05，进阶篇 01–08），跨篇引用会标注「进阶篇 NN」。

```
            ┌──────────── 基础篇（Basics 01–05）────────────┐
   入口  ──►  01 为什么需要窗口管理器（动机）
            02 GLFW / GLEW 核心概念（扫盲）
            03 RAII 与 C++ 封装思想（内功）
            04 实现基础 GLFWManager（核心代码）  ◄── 想速成就直接读这篇
            05 用 Manager 写出第一个窗口（实战）
            └──────────────────────────────────────────────┘
                              │
                              ▼
            ┌──────────── 进阶篇（Advanced 01–08）──────────┐
            01 事件系统设计（Event / Dispatcher）
            02 窗口抽象层与接口设计（Window 接口）
            03 输入系统（轮询 vs 事件）
            04 分离 RenderContext（职责单一）
            05 集成 ImGui（调试面板）
            06 多窗口与生命周期管理（工程化）
            07 性能与多线程 ★ 新增
            08 测试与验证 ★ 新增
            └──────────────────────────────────────────────┘
```

**建议顺序**：基础篇 01 → 05 顺序读；进阶篇可以按需挑读，但 01（事件系统）是 02/03 的前置，
06（生命周期）是 07（多线程）的前置。

只想看「最终长什么样」？直接跳到 [基础篇 04 实现基础 GLFWManager](basics/04-实现基础GLFWManager.md)。

---

## 目录

### 基础篇

| 编号 | 标题 | 一句话 |
|:---:|---|---|
| [01](basics/01-为什么需要窗口管理器.md) | 为什么需要窗口管理器 | 脚本式 vs 引擎式，痛点与解耦 |
| [02](basics/02-GLFW与GLEW核心概念.md) | GLFW 与 GLEW 核心概念 | 上下文、函数指针加载、主循环 |
| [03](basics/03-RAII与C++封装思想.md) | RAII 与 C++ 封装思想 | 资源管理、三五法则、前向声明 |
| [04](basics/04-实现基础GLFWManager.md) | 实现基础 GLFWManager | 完整 `.h` / `.cpp` 逐行讲解 |
| [05](basics/05-用Manager写出第一个窗口.md) | 用 Manager 写出第一个窗口 | 改写 main、验证清单、排错 |

### 进阶篇

| 编号 | 标题 | 一句话 |
|:---:|---|---|
| [01](advanced/01-事件系统设计.md) | 事件系统设计 | Event 基类 + EventDispatcher |
| [02](advanced/02-窗口抽象层与接口设计.md) | 窗口抽象层与接口设计 | Window 接口 + 工厂模式 |
| [03](advanced/03-输入系统.md) | 输入系统 | Input 单例、轮询抽象 |
| [04](advanced/04-分离RenderContext.md) | 分离 RenderContext | 窗口与上下文职责分离 |
| [05](advanced/05-集成ImGui.md) | 集成 ImGui | nativeHandle 的真正用途 |
| [06](advanced/06-多窗口与生命周期管理.md) | 多窗口与生命周期管理 | 引用计数、进程级资源 |
| [07](advanced/07-性能与多线程.md) | 性能与多线程 | 帧时间、固定步长、异步加载、线程安全 |
| [08](advanced/08-测试与验证.md) | 测试与验证 | doctest、AAA、纯逻辑测试、Fake |

### 拓展篇（Extra）

独立成篇的专题深潜，按需选读，相互以「下一篇 Extra」串成链：

| 标题 | 一句话 |
|---|---|
| [`std::function` 完全指南](extra/std-function完全指南.md) | 类型擦除、SBO、什么时候不该用 |
| [`std::bind` / lambda / `std::ref` 完全指南](extra/std-bind-lambda-ref完全指南.md) | 造可调用对象的三种手段与取舍 |
| [事件系统架构全景：七种模式对比](extra/事件系统架构全景-七种模式对比.md) | Dispatcher / Signal-Slot / EventQueue / PubSub / ECS / Rx |
| [Signal/Slot 信号槽完全指南](extra/Signal-Slot信号槽完全指南.md) ★ 新增 | 多播回调、连接管理、自研 MiniSignal 与 Boost.Signals2 全解（练习含全部答案） |
| [OS 信号处理 signal 完全指南](extra/OS信号处理signal完全指南.md) ★ 新增 | Ctrl+C、异步信号安全、优雅退出引擎主循环（练习含全部答案） |

---

## 项目环境说明

本教程基于本仓库现有的工程结构：

```
opengl/
├── CMakeLists.txt          # C++17, GLOB src/*.cpp 和 *.h 自动入编译
├── include/                # GLFW / GLEW / GLM 的公共头
├── lib/                    # glfw-3.4, glew-2.3.1, glm（源码依赖）
└── src/                    # 你的代码
    ├── main.cpp
    ├── GLFWManager.h       # ← 本教程的主角
    └── GLFWManager.cpp     # ← 本教程的主角
```

> ⚠️ 注意：仓库里现在的头文件名是 `GLFWManaager.h`（3 个 a），和 `GLFWManager.cpp`（2 个 a）对不上。
> 教程统一使用 **`GLFWManager.h`**，请先把文件名改对，否则 `#include` 会找不到。

**编译**（任意一种）：

```bash
cmake -B build && cmake --build build --config Debug
```

你不需要手动把新文件加进 CMake——`CMakeLists.txt` 用 `file(GLOB_RECURSE ...)` 已经会自动收集 `src/` 下的 `.cpp/.h/.c`。只要文件放进 `src/`，重新 configure 即可。

---

## 约定与符号

- 💡 **提示**：补充说明或小技巧。
- ⚠️ **坑**：容易踩的错误，配排错方法。
- 🔧 **练习**：留给你的动手题，答案不唯一。
- 📦 **代码块顶部标了来源**：能直接放进对应文件。

所有示例代码的 OpenGL 版本：**4.1 Core**（与 `main.cpp` 现有 `glfwWindowHint` 一致）。

---

## 参考与延伸阅读

- **GLFW 官方文档**：<https://www.glfw.org/docs/latest/>（回调、窗口、输入、多线程的权威来源）
- **TheCherno 的 Hazel 引擎系列**（YouTube）：窗口抽象 + 事件系统这套模式在工业界最常被模仿，本教程的进阶篇大量借鉴其思路。
- **《Game Engine Architecture》Jason Gregory**：讲清楚为什么引擎要分层、要抽象。
- **LearnOpenGL** <https://learnopengl.com/>：OpenGL 本身的 API 细节。
- **《Fix Your Timestep!》Glenn Fiedler**：<https://gafferongames.com/post/fix_your_timestep/>（进阶篇 07 固定步长的经典出处）
- **doctest 文档**：<https://github.com/doctest/doctest/blob/master/doc/markdown/readme.md>（进阶篇 08 测试框架）

---

准备好就开始吧 👉 [基础篇 01 为什么需要窗口管理器](basics/01-为什么需要窗口管理器.md)
