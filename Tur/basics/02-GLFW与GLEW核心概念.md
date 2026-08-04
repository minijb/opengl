# 02 · GLFW 与 GLEW 核心概念

> 目标：在动手封装之前，先弄清楚我们到底在封装什么——GLFW 干嘛的、GLEW 干嘛的、
> 为什么需要它们俩配合，以及「上下文」「双缓冲」「主循环」这些词到底指什么。

---

## 2.1 一张图看懂分工

```
你的代码
   │
   ├── GLFW ────────── 负责「窗口和输入」
   │      创建窗口、处理键盘/鼠标、事件循环、跨平台（Win/Mac/Linux）
   │      ❌ 不负责加载 OpenGL 函数
   │
   ├── GLEW ────────── 负责「加载 OpenGL 函数指针」
   │      因为 glDrawArrays 之类的函数地址是运行时才知道的，
   │      不同驱动、不同显卡地址都不同，必须动态获取
   │
   └── OpenGL 驱动 ──── 真正画图的（显卡里的）
```

**一句话记忆**：GLFW 给你「一块能画图的窗」，GLEW 帮你「找到画图用的那些函数」。

> 💡 还有第三种选择叫 **GLAD**（你 `src/glad.c` 那个就是）。GLAD 和 GLEW 干同一件事（加载函数指针），
> 只是更现代、可定制。本仓库 `main.cpp` 用的是 GLEW，所以教程统一用 GLEW；原理完全互通。

---

## 2.2 OpenGL Context（上下文）

「上下文」是 OpenGL 里最重要的概念之一，也是一个大坑源。

**它是什么**：一个保存所有 OpenGL 状态的「工作台」——当前绑定了哪个 shader、哪个纹理、viewport 多大、
哪块 framebuffer……所有这些状态都挂在上下文上。

**关键规则**：

1. **一个线程同一时刻只有一个「当前上下文」**。你要画图，必须先 `glfwMakeContextCurrent(window)`。
2. **上下文和窗口绑定**。GLFW 创建窗口时一并创建了上下文，`MakeContextCurrent` 把它设为当前。
3. **加载函数指针（GLEW）必须在上下文 current 之后**，否则没有上下文，去哪找地址？
   - ❌ 错误顺序：`glewInit()` 在 `glfwMakeContextCurrent()` 之前
   - ✅ 正确顺序：建窗 → `MakeContextCurrent` → `glewInit()`

```cpp
GLFWwindow* w = glfwCreateWindow(...);
glfwMakeContextCurrent(w);   // ← 上下文变 current
glewInit();                  // ← 现在才能加载函数指针
```

这条顺序规则会在 [04 篇](04-实现基础GLFWManager.md) 里直接体现。

---

## 2.3 为什么需要 GLEW？函数指针是「运行时获取」的

你可能以为 `glDrawArrays` 是个普通函数，链接器帮你找地址。**不是。**

在 Windows 上，`opengl32.dll` 只导出 OpenGL 1.1 的那一百来个老函数。OpenGL 2.0+（shader、VBO、
FBO……）的函数，驱动是按显卡不同「临时」提供的，地址运行时才知道。

所以你必须：

```cpp
// GLEW 帮你干这件事（伪代码）：
glDrawArrays = (PFNGLDRAWARRAYSPROC)wglGetProcAddress("glDrawArrays");
```

每换一张显卡、每换一个驱动版本，地址都可能不同。GLEW 一次性帮你把几千个函数指针全填好，
填好后你才能像普通函数一样调用 `glDrawArrays(...)`。

> ⚠️ **坑**：core profile 下通常要 `glewExperimental = GL_TRUE;` 再 `glewInit()`，
> 否则很多扩展函数加载不到、调用就崩——这是 GLEW 1.x 时代的经典坑。
> **GLEW 2.x 起基本不需要它了**：2.x 枚举扩展走 `glGetIntegerv(GL_NUM_EXTENSIONS)`，
> 这个变量属于遗留习惯（教程保留这一行，设了也无害）。这一行在 04 篇的实现里有。

---

## 2.4 双缓冲（Double Buffering）与主循环

显示器每秒刷新 60 次。如果你直接往屏幕上画，画到一半就被显示器读走 → **画面撕裂**。

**双缓冲**用两块画布解决：

```
   [前缓冲]  ──► 显示器在看这块
   [后缓冲]  ──► 你在背面偷偷画这块
        │
        └─画完一帧，两块“交换”（swap）► 显示器改看新画好的，你拿到旧的继续画
```

对应代码就一句：

```cpp
glfwSwapBuffers(window);   // 把后缓冲推上去，交换前后
```

而**主循环（game loop）**是游戏 / 引擎的心跳：

```
  while (窗口没被关) {
      处理输入         ── glfwPollEvents()
      更新游戏状态     ── 你的逻辑
      渲染一帧到后缓冲 ── glClear / glDraw...
      交换缓冲         ── glfwSwapBuffers()
  }
```

- `glfwPollEvents()`：**拉取**操作系统塞进来的事件（按键、鼠标、缩放），并触发你注册的回调。
  不调用它，窗口会卡死、标题栏点不动。
- `glfwSwapBuffers()`：交一帧给显示器。

> 💡 这就是为什么循环里这两句缺一不可，而且**顺序**通常是：先渲染再 swap、最后 poll（或先 poll 再渲染，
> 两种都对，看你时序偏好）。本教程采用「渲染 → swap → poll」。

---

## 2.5 VSync（垂直同步）

`glfwSwapInterval(1)` 告诉显卡：「swap 时等显示器垂直同步信号」，把帧率上限锁在刷新率（通常 60/120/144）。

- `0`：不等待，能多快多快（CPU/GPU 拉满，可能撕裂）。
- `1`：每个刷新信号 swap 一次（默认推荐）。
- `-1`：自适应 VSync（驱动支持的话）。

引擎里通常做成开关：`window.setVsync(true/false)`，对应内部 `glfwSwapInterval`。04 篇会实现。

---

## 2.6 GLFW 是 C API，回调是函数指针

这是封装时最大的难点，提前理解。

GLFW 注册回调的函数长这样：

```cpp
glfwSetKeyCallback(window, MyKeyCallback);
//                            ^^^^^^^^^^^^^
//                     必须是普通函数 / 静态函数，签名为
//                     void(GLFWwindow*, int, int, int, int)
```

它**不能**接收 C++ 的成员函数（成员函数有隐含的 `this`，签名对不上），
也**不能**直接用 lambda 捕获（捕获的 lambda 不是可转换成普通函数指针的）。

所以「C 回调里怎么访问 C++ 对象？」——靠 **user pointer**：

```cpp
glfwSetWindowUserPointer(window, this);          // 把 this 挂到窗口上
// 回调里：
void callback(GLFWwindow* w, ...) {
    auto* self = static_cast<MyClass*>(glfwGetWindowUserPointer(w));  // 取回来
    self->doSomething();                          // 现在能调成员了
}
```

这套「**静态回调 + user pointer 桥接回 this**」是几乎所有 C++ 封装 C 库的标准手法。
[04 篇](04-实现基础GLFWManager.md) 会完整实现一遍。务必先理解这个机制。

---

## 2.7 本仓库用到的具体版本

看你的 `CMakeLists.txt` 和 `lib/`：

| 库 | 版本 | 作用 |
|---|---|---|
| GLFW | 3.4 | 窗口 / 输入 |
| GLEW | 2.3.4 | 加载 GL 函数指针（`glew_s` 静态库） |
| GLM | 最新 | 数学库（向量、矩阵），和窗口无关，渲染才用 |
| OpenGL | 4.1 Core | 目标 GL 版本（`main.cpp` 里 `CONTEXT_VERSION 4,1`） |

> ⚠️ macOS 上 4.1 是支持上限，且必须加 `glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE)`。
> Windows / Linux 通常能上到 4.6。教程为兼容统一写 4.1，你在 Windows 上想用 4.6 直接改 hint 即可。

> 📌 版本以 `lib/` 内头文件为准：`glew-2.3.1/` 目录名的版本号已过时，头文件 `glew.h` 里的
> 版本宏是 **2.3.4**（`GLEW_VERSION_MINOR 3 / MICRO 4`）。

---

## 2.8 小结

- **GLFW 管窗口和输入，GLEW 管加载函数指针，两者职责正交，缺一不可。**
- **上下文（context）是 OpenGL 状态的容器**，必须先 current 才能初始化 GLEW。
- **双缓冲 + 主循环**是实时渲染的基础结构。
- **GLFW 回调是 C 函数指针**，访问 C++ 对象要靠 user pointer——这是封装的核心机制。

---

## 🔧 练习

1. 试着解释给一个没接触过 OpenGL 的人听：为什么 `glewInit()` 必须在 `glfwMakeContextCurrent()` 之后？
2. 把 VSync 从 1 改成 0，观察你的窗口 CPU 占用变化，体会「不等待」的代价。
3. （进阶）思考：如果同一个进程开了两个窗口、两个上下文，画图时怎么知道画到哪个窗口？→ 答案在 [进阶篇 06](../advanced/06-多窗口与生命周期管理.md)。

## 📝 参考答案

### 1. 为什么 `glewInit()` 必须在 `glfwMakeContextCurrent()` 之后？

`glewInit()` 的工作是用 `wglGetProcAddress` / `glXGetProcAddress` 去**问驱动**拿 OpenGL 函数（如 `glDrawArrays`）的真实地址。这些函数指针挂在 **GL 上下文**上，没有 current 上下文就没有「问地址」的入口。`glfwMakeContextCurrent` 才是「把上下文设为当前线程活动上下文」的动作——没做这步，驱动无法定位函数表，`glewInit` 拿到的是垃圾/NULL，后续调用即崩。所以顺序锁死：**建窗 → MakeContextCurrent → glewInit**。

### 2. VSync 1→0 的 CPU 占用变化

`glfwSwapInterval(0)` 表示 swap 不等垂直同步信号，循环「能多快跑多快」。结果：主循环不再被 60Hz 阻塞，**CPU 一核拉满（~100%）、GPU 也拉满**，帧率远超刷新率（可能上千 FPS），画面可能撕裂。这就是「不等待」的代价——用满载换低延迟，用功耗/发热/撕裂换帧率上限。

### 3. 两个窗口、两个上下文，怎么知道画到哪个？

画图前调 `glfwMakeContextCurrent(目标窗口)`，把**那个窗口的上下文**设为当前线程的活动上下文。GL 所有调用只作用于「当前上下文」。所以多窗口渲染就是：`makeContextCurrent(w1) → 画 w1 → swap(w1) → makeContextCurrent(w2) → 画 w2 → swap(w2)`。注意 `glfwPollEvents()` 是进程级、只调一次。详见 [进阶 06 6.5](../advanced/06-多窗口与生命周期管理.md)。

---

下一篇 👉 [03 RAII 与 C++ 封装思想](03-RAII与C++封装思想.md)
