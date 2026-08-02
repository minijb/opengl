# 04 · 实现基础 GLFWManager

> 目标：这一篇是全套教程的「主菜」。我们会写出一个完整的、能直接放进你 `src/` 编译运行的 `GLFWManager`。
> 代码不长，但每一行都对应前面 01~03 讲过的道理。建议边读边在自己项目里建文件。

---

## 4.0 准备：先把文件名搞对

仓库现状（坑）：

```
src/GLFWManager.cpp     ← 2 个 a
src/GLFWManaager.h      ← 3 个 a  ❌
```

**把头文件重命名为 `src/GLFWManager.h`**，和 `.cpp` 对齐。否则下面 `#include "GLFWManager.h"` 找不到。

---

## 4.1 先想清楚接口（API 设计先行）

在写实现前，先列出「业务代码」需要从窗口拿到什么能力。对着 [01 篇](01-为什么需要窗口管理器.md) 的练习对一下：

| 能力 | 方法 |
|---|---|
| 创建并初始化窗口 | `bool init(Props)` |
| 销毁窗口 | `void shutdown()` |
| 主循环三件套 | `pollEvents()` / `swapBuffers()` / `shouldClose()` |
| 主动关闭 | `setShouldClose(bool)` |
| 查询尺寸 | `width()` / `height()` |
| VSync 开关 | `setVsync(bool)` |
| 输入查询 | `isKeyPressed(int key)` |
| 订阅按键/缩放事件 | `setKeyCallback(...)` / `setResizeCallback(...)` |
| 给 ImGui 等用 | `nativeHandle()` |

> 💡 **设计原则**：接口里一个 `glfw` 都不出现。能用纯语义（shouldClose、swapBuffers）表达的，就不用库术语。

---

## 4.2 头文件 `src/GLFWManager.h`

📦 完整内容，可直接覆盖：

```cpp
#pragma once

#include <functional>
#include <string>

// 前向声明：不在头里暴露 GLFW（见 03 篇 3.5）
struct GLFWwindow;

class GLFWManager {
public:
    // 创建窗口的配置：聚合初始化即可 window.init({"Title", 800, 600})
    struct Props {
        std::string title{"OpenGL App"};
        int  width {800};
        int  height{600};
        bool fullscreen{false};
        bool vsync     {true};
    };

    // 用户可订阅的事件回调（解耦：上层关心“发生了什么”，不关心 GLFW）
    using KeyCallback    = std::function<void(int key, int scancode, int action, int mods)>;
    using ResizeCallback = std::function<void(int width, int height)>;

    GLFWManager();
    ~GLFWManager();

    // 窗口是独占资源，禁用拷贝（见 03 篇 3.4）
    GLFWManager(const GLFWManager&)            = delete;
    GLFWManager& operator=(const GLFWManager&) = delete;

    bool init(const Props& props = {});   // 两段式构造，失败返回 false
    void shutdown();

    // —— 主循环 API ——
    void pollEvents();
    void swapBuffers();
    bool shouldClose() const;
    void setShouldClose(bool v);

    // —— 属性 ——
    int  width()  const { return m_width;  }
    int  height() const { return m_height; }
    void setVsync(bool enabled);

    // —— 事件订阅 ——
    void setKeyCallback(KeyCallback cb)       { m_keyCb    = std::move(cb); }
    void setResizeCallback(ResizeCallback cb) { m_resizeCb = std::move(cb); }

    // —— 输入查询（轮询式）——
    bool isKeyPressed(int key) const;

    // 逃生舱口：仅在需要原生句柄（如 ImGui）时用，业务代码不要碰
    GLFWwindow* nativeHandle() const { return m_window; }

private:
    // GLFW 回调是 C 函数指针，必须 static；靠 user pointer 找回 this（见 02 篇 2.6）
    static void keyCallback(GLFWwindow* w, int key, int scancode, int action, int mods);
    static void framebufferSizeCallback(GLFWwindow* w, int width, int height);

    GLFWwindow*    m_window{nullptr};
    int            m_width{0};
    int            m_height{0};
    KeyCallback    m_keyCb;
    ResizeCallback m_resizeCb;
};
```

### 逐段说明

- **`struct Props`**：用默认成员初始化 `{...}`，调用方写 `init({"Demo", 800, 600})` 省心，不写全也有合理默认值。
- **`using KeyCallback = std::function<...>`**：定义回调签名类型。`std::function` 能装「普通函数、lambda、
  仿函数、成员函数（配 bind）」，是最灵活的回调载体。
- **`= delete` 拷贝**：[03 篇](03-RAII与C++封装思想.md) 讲过，独占资源禁止拷贝。
- **`init()` 返回 `bool`**：两段式构造，错误处理交给调用方。
- **`width()/height()` 内联**：这种一行 getter 内联在头里，避免为读尺寸付一次函数调用开销。
- **`static` 回调**：C 函数指针要普通函数，成员函数不行。后面 `.cpp` 会用 user pointer 桥接。

---

## 4.3 实现文件 `src/GLFWManager.cpp`

📦 完整内容，可直接覆盖：

```cpp
#include "GLFWManager.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <iostream>

// 把 GLFW 的错误转成可读日志（调试利器）
static void glfwErrorCallback(int code, const char* desc) {
    std::cerr << "[GLFW Error " << code << "] " << desc << "\n";
}

GLFWManager::GLFWManager() = default;

GLFWManager::~GLFWManager() { shutdown(); }

bool GLFWManager::init(const Props& props) {
    // (1) glfwInit 是进程级的，整个程序只调一次（见 02 篇 + 11 篇）
    static bool glfwInited = false;
    if (!glfwInited) {
        glfwSetErrorCallback(glfwErrorCallback);
        if (!glfwInit()) {
            std::cerr << "Failed to initialize GLFW\n";
            return false;
        }
        glfwInited = true;
    }

    // (2) 声明需要的 OpenGL 版本与 profile
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);   // macOS 必须
#endif

    // (3) 创建窗口（可选全屏）
    GLFWmonitor* monitor = props.fullscreen ? glfwGetPrimaryMonitor() : nullptr;
    m_window = glfwCreateWindow(props.width, props.height,
                                props.title.c_str(), monitor, nullptr);
    if (!m_window) {
        std::cerr << "Failed to create GLFW window\n";
        return false;
    }

    // (4) 关键：把 this 挂到窗口上，C 回调才能找回 C++ 实例（见 02 篇 2.6）
    glfwSetWindowUserPointer(m_window, this);
    glfwSetKeyCallback(m_window, keyCallback);
    glfwSetFramebufferSizeCallback(m_window, framebufferSizeCallback);

    // (5) 激活上下文后才能加载 GL 函数指针（见 02 篇 2.2 / 2.3）
    glfwMakeContextCurrent(m_window);

    static bool glewInited = false;
    if (!glewInited) {
        glewExperimental = GL_TRUE;                 // core profile 下必须开（见 02 篇 2.3）
        if (glewInit() != GLEW_OK) {
            std::cerr << "Failed to initialize GLEW\n";
            return false;
        }
        glewInited = true;
    }

    // (6) 记录尺寸、设 VSync
    m_width  = props.width;
    m_height = props.height;
    setVsync(props.vsync);
    return true;
}

void GLFWManager::shutdown() {
    if (m_window) {
        glfwDestroyWindow(m_window);   // 只销毁窗口
        m_window = nullptr;
    }
    // 故意不调 glfwTerminate()：它是进程级的，
    // 由程序退出时回收，或在最外层显式调一次。详见 11 篇。
}

void GLFWManager::pollEvents()           { glfwPollEvents(); }
void GLFWManager::swapBuffers()          { glfwSwapBuffers(m_window); }
bool GLFWManager::shouldClose() const    { return glfwWindowShouldClose(m_window); }
void GLFWManager::setShouldClose(bool v) { glfwSetWindowShouldClose(m_window, v); }
void GLFWManager::setVsync(bool enabled) { glfwSwapInterval(enabled ? 1 : 0); }

bool GLFWManager::isKeyPressed(int key) const {
    return m_window && glfwGetKey(m_window, key) == GLFW_PRESS;
}

// —— 静态回调：从 window 取回 this，再分发给用户的 std::function ——
void GLFWManager::keyCallback(GLFWwindow* w, int key, int sc, int action, int mods) {
    auto* self = static_cast<GLFWManager*>(glfwGetWindowUserPointer(w));
    if (self && self->m_keyCb) self->m_keyCb(key, sc, action, mods);
}

void GLFWManager::framebufferSizeCallback(GLFWwindow* w, int width, int height) {
    auto* self = static_cast<GLFWManager*>(glfwGetWindowUserPointer(w));
    if (!self) return;
    self->m_width  = width;            // 同步尺寸，否则 width()/height() 会过时
    self->m_height = height;
    if (self->m_resizeCb) self->m_resizeCb(width, height);
}
```

> ⚠️ **常见错误**：忘记在 `framebufferSizeCallback` 里同步 `m_width/m_height`，结果用户 resize 窗口后
> 调 `width()` 拿到的还是创建时的旧尺寸，渲染 viewport 就错位了。务必在回调里更新这两个成员。

---

## 4.4 实现里的 6 个关键决策（重点）

### 决策 1：`glfwInit` 用 `static bool` 只调一次

```cpp
static bool glfwInited = false;
if (!glfwInited) { glfwInit(); ... glfwInited = true; }
```

为什么？`glfwInit` / `glfwTerminate` 是**进程级**的，跟窗口数量无关。
你在 [进阶篇 06](../advanced/06-多窗口与生命周期管理.md) 会看到，多个 `GLFWManager` 实例时，第一个负责 init，后来的跳过。
本篇只有一个窗口，但这个习惯先养成。

### 决策 2：`shutdown()` 不调 `glfwTerminate()`

`glfwTerminate()` 会**销毁所有窗口、所有游标、撤销整个 GLFW**。如果放进单个窗口的析构，
第二个窗口还没建出来就被清场了。所以析构只 `glfwDestroyWindow`（销毁自己这个窗口），
`glfwTerminate` 留给程序最外层、且只调一次。详见 [进阶篇 06](../advanced/06-多窗口与生命周期管理.md)。

### 决策 3：错误用 `std::cerr` + 返回 `false`

不再 `exit(EXIT_FAILURE)`。调用方可以选择：弹框、回退、记日志、重试。

### 决策 4：静态回调 → user pointer → 成员 `std::function`

```
glfwSetKeyCallback(window, &GLFWManager::keyCallback);   // 静态
        │ GLFW 收到按键，调 keyCallback(window, ...)
        ▼
   glfwGetWindowUserPointer(window) == this               // 取回实例
        │
        ▼
   this->m_keyCb(key, ...)                                // 调用用户注册的 lambda
```

这是 [02 篇 2.6](02-GLFW与GLEW核心概念.md) 的机制落地。两层跳转，但把 C API 彻底驯服了。

### 决策 5：`framebufferSizeCallback` 必须同步 `m_width/m_height`

GLFW 的窗口尺寸（`glfwCreateWindow`）和**可绘制区域**（framebuffer）尺寸不一定相等——
Retina/HiDPI 屏上 framebuffer 是窗口尺寸的 2 倍。这里用 framebuffer 回调同步 `m_width/m_height`，
保证用户调 `width()` 拿到的是真实的可绘制尺寸。否则 resize 后渲染会错位。

### 决策 6：`isKeyPressed` 判空

```cpp
return m_window && glfwGetKey(m_window, key) == GLFW_PRESS;
```

如果 `init` 还没调（`m_window == nullptr`），调 `glfwGetKey(nullptr, ...)` 是 UB。加个 `&&` 守卫，安全降级返回 `false`。

---

## 4.5 关于 `Props` 默认值的小聪明

```cpp
struct Props {
    std::string title{"OpenGL App"};
    int  width {800};
    int  height{600};
    ...
};
```

C++11 起支持「类内成员默认初始化」。配合聚合初始化，调用方能：

```cpp
window.init();                       // 全用默认
window.init({"Demo"});               // 只改标题
window.init({"Demo", 1280, 720});    // 标题 + 尺寸
window.init({.title="Demo", .vsync=false});  // C++20 指定初始化，挑字段改
```

> ⚠️ 最后那种「指定初始化」要 C++20，你 `CMakeLists.txt` 设的是 C++17，要用就先升级标准。
> C++17 下只能按顺序传。

---

## 4.6 小结

到这一步你应该拥有：
- `src/GLFWManager.h`（接口，零 GLFW 依赖）
- `src/GLFWManager.cpp`（实现，所有 GLFW 调用都在这）

它已经是个**能用的**窗口管理器。下一篇我们把它接进 `main.cpp`，跑出你的第一个「引擎式」窗口。

> 💡 如果你现在就 build，会因为 `main.cpp` 还没改而报「`init` 之类没用到」——没关系，先别 build，
> 等下一篇改完 `main.cpp` 一起跑。

---

## 🔧 练习

1. 把 `shutdown()` 改成「可重复调用」（多次调用不崩）。现在第二次调用时 `m_window` 已经是 `nullptr`，靠 `if (m_window)` 守卫——验证这个守卫确实生效。
2. 加一个 `setTitle(const std::string&)` 方法，内部调 `glfwSetWindowTitle`。想想接口要不要暴露 GLFW？答案：不要，参数用 `std::string`。
3. （思考）如果用户在 `init()` 之后又调了一次 `init()`，会发生什么？现在代码会泄漏第一个窗口。加一个守卫：`if (m_window) shutdown();` 放在 `init` 开头。

## 📝 参考答案

### 1. 让 `shutdown()` 可重复调用

现状已有 `if (m_window)` 守卫，验证它生效：

```cpp
void GLFWManager::shutdown() {
    if (m_window) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;   // 关键：置空，第二次调用看到 nullptr 直接跳过
    }
}
```

第一次调用销毁并置 `nullptr`；第二次 `if (m_window)` 为假，直接返回。**多次调用安全（幂等）**。

### 2. 加 `setTitle`

头：

```cpp
void setTitle(const std::string& title);
```

实现（参数用 `std::string`，不暴露 GLFW）：

```cpp
void GLFWManager::setTitle(const std::string& title) {
    if (m_window) glfwSetWindowTitle(m_window, title.c_str());
}
```

### 3.（思考）`init()` 调两次会泄漏 + 加守卫

调两次 `init()`：第二次 `glfwCreateWindow` 覆盖 `m_window`，**第一个窗口句柄丢失 → 泄漏**。守卫加在 `init` 开头：

```cpp
bool GLFWManager::init(const Props& props) {
    if (m_window) shutdown();   // 已初始化过，先清掉旧的
    // ... 原 init 逻辑
}
```

---

下一篇 👉 [05 用 Manager 写出第一个窗口](05-用Manager写出第一个窗口.md)
