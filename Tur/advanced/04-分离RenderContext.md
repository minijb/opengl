# 进阶 04 · 分离 RenderContext

> 目标：到目前为止，`GLFWManager` / `GLFWWindowImpl` 既管窗口又管 GL 上下文初始化（GLEW）。
> 职责越少越好——本篇把「创建/管理 GL 上下文 + 加载函数指针」单独抽出来，让 Window 只管窗口本身。

---

## 4.1 现状：一个类干了三件事

[基础篇 04](../basics/04-实现基础GLFWManager.md) 的 `init()` 里其实混了三个职责：

```
init() {
    glfwInit() / glfwCreateWindow()   ← ① 窗口管理
    glfwMakeContextCurrent()          ← ② 上下文激活
    glewInit()                        ← ③ 函数指针加载
}
```

为什么这是问题？

1. **职责耦合**：换 OpenGL 加载库（GLEW → GLAD → gl3w）时，要改的是「窗口类」——明明加载和窗口无关。
2. **换后端难**：Vulkan、Metal、DirectX 的上下文初始化和 GL 完全不同，全堆在窗口类里会很乱。
3. **测试难**：想测「窗口能建出来」得连 GL 也一起初始化；想测「渲染」得开真窗口。

**单一职责原则（SRP）**：一个类，一个变化的理由。窗口的变化理由是「换窗口库」，上下文的变化理由是「换图形 API」——
分开。

---

## 4.2 目标结构

```
Window          ← 只管窗口、事件、输入（不含任何 GL）
   │ 持有
   ▼
RenderContext   ← 管 GL 上下文 + 函数指针加载
   ├─ OpenGLContext (GLEW/GLAD)
   ├─ (未来) VulkanContext
   └─ (未来) MetalContext
```

`Window` 创建窗口后，把原生句柄交给 `RenderContext::init()`，由它完成「make current + load functions」。

---

## 4.3 RenderContext 接口

```cpp
// RenderContext.h   （core 接口）
#pragma once
class RenderContext {
public:
    virtual ~RenderContext() = default;

    // nativeWindow 是平台相关的原生句柄（GLFWwindow* / SDL_Window* / HWND），
    // 用 void* 屏蔽，core 层不认识具体类型
    virtual void init(void* nativeWindow) = 0;

    virtual void swapBuffers() = 0;        // 交换缓冲
    virtual void setVsync(bool enabled) = 0;
    virtual bool isVsync() const = 0;

    // 工厂：按引擎选定的图形 API 创建对应上下文
    static std::unique_ptr<RenderContext> create(void* nativeWindow);
};
```

---

## 4.4 OpenGL 实现（把 GLEW 搬进来）

```cpp
// OpenGLContext.h   （platform）
#pragma once
#include "RenderContext.h"
struct GLFWwindow;   // 或者用 void*

class OpenGLContext : public RenderContext {
public:
    OpenGLContext(GLFWwindow* window);   // 构造时记下窗口
    void init(void* nativeWindow) override;
    void swapBuffers() override;
    void setVsync(bool enabled) override;
    bool isVsync() const override { return m_vsync; }
private:
    GLFWwindow* m_window;
    bool m_vsync{true};
};
```

```cpp
// OpenGLContext.cpp   （platform，include GLEW + GLFW）
#include "OpenGLContext.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <iostream>

OpenGLContext::OpenGLContext(GLFWwindow* window) : m_window(window) {}

void OpenGLContext::init(void* /*nativeWindow*/) {
    glfwMakeContextCurrent(m_window);

    // GLEW 只在进程里初始化一次
    static bool inited = false;
    if (!inited) {
        glewExperimental = GL_TRUE;
        if (glewInit() != GLEW_OK) {
            std::cerr << "Failed to initialize GLEW\n";
            return;
        }
        inited = true;
    }
    std::cout << "OpenGL " << glGetString(GL_VERSION) << "\n";
}

void OpenGLContext::swapBuffers()         { glfwSwapBuffers(m_window); }
void OpenGLContext::setVsync(bool v)      { glfwSwapInterval(v ? 1 : 0); m_vsync = v; }
```

工厂：

```cpp
// RenderContext.cpp   （platform）
#include "RenderContext.h"
#include "OpenGLContext.h"
std::unique_ptr<RenderContext> RenderContext::create(void* nativeWindow) {
#if defined(RENDER_API_OPENGL)
    return std::make_unique<OpenGLContext>(static_cast<GLFWwindow*>(nativeWindow));
#elif defined(RENDER_API_VULKAN)
    return std::make_unique<VulkanContext>(nativeWindow);
#endif
}
```

---

## 4.5 Window 改造：只管窗口，把上下文委托出去

`GLFWWindowImpl::init()` 瘦身：

```cpp
bool GLFWWindowImpl::init(const Props& props) {
    // ...只做 glfwInit / glfwCreateWindow / 注册回调（同基础篇 04）

    // 创建上下文，把原生句柄交出去
    m_context = RenderContext::create((void*)m_window);
    m_context->init((void*)m_window);

    // setVsync / swapBuffers 改成转调 m_context
    m_context->setVsync(props.vsync);
    return true;
}

void GLFWWindowImpl::swapBuffers()       { m_context->swapBuffers(); }
void GLFWWindowImpl::setVsync(bool v)    { m_context->setVsync(v); }
```

现在 `Window` 类里**没有 `glew`、没有 `glGetString`**——它只负责「让一块像素区域出现在屏幕上并接收事件」，
「怎么渲染」完全是 `RenderContext` 的事。

---

## 4.6 上下文的生命周期与窗口绑定

一个关键约束：**上下文必须和窗口同生共死**。窗口没了，上下文也就无效了。

```
窗口创建 ──► 上下文创建并 init ──► 渲染循环 ──► 上下文随窗口析构
   m_window                            m_context
   (先创建)                            (后创建)
                      ▲
                      └─ 析构顺序：m_context 先于 m_window（成员声明顺序反过来）
```

> ⚠️ **成员声明顺序 = 析构顺序（逆序）**。要让 `m_context` 先析构（释放 GL 资源），再析构 `m_window`（销毁窗口），
> 否则窗口先没了、上下文还在引用它就崩。声明时写：
> ```cpp
> class GLFWWindowImpl {
>     std::unique_ptr<RenderContext> m_context;   // 先声明 → 后析构
>     GLFWwindow* m_window;                       // 后声明 → 先析构 ❌ 顺序错了！
> };
> ```
> 正确顺序应该是 `m_window` 先析构？不——**上下文要在窗口销毁前清理 GL 资源**，
> 所以 `m_context` 必须先析构，声明在 `m_window` **之前**。仔细想清楚这点，是 GL 程序的常见崩溃源。

实际上更稳妥：在 `shutdown()` 里**显式**先 reset context 再 destroy window，不依赖成员声明顺序：

```cpp
void GLFWWindowImpl::shutdown() {
    m_context.reset();           // 先释放上下文（GL 资源）
    if (m_window) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
}
```

显式顺序，不靠编译器默认行为——这才是工程化。

---

## 4.7 这样拆的好处

| 场景 | 不拆（旧） | 拆了 RenderContext |
|---|---|---|
| 换 GL 加载库 GLEW→GLAD | 改窗口类 | 只改 `OpenGLContext.cpp` |
| 加 Vulkan 后端 | 窗口类里塞一堆 vk 代码 | 加 `VulkanContext`，窗口类不动 |
| Headless 渲染（无窗口） | 几乎做不到 | `Window` 用 headless 实现，`Context` 照常 |
| 单测「窗口能创建」 | 必须连带初始化 GL | 窗口测试不碰 GL |

---

## 4.8 小结

- `Window` + `RenderContext` 分离，遵循单一职责：窗口管像素区域 + 事件，上下文管 GL 状态 + 函数加载。
- 上下文用 `void*` 接收原生句柄，core 接口零后端依赖。
- **析构顺序是 GL 程序的隐形杀手**：上下文必须在窗口销毁前清理——用显式 `shutdown()` 控制顺序，别赌成员声明顺序。
- 这种「接口 + 多实现 + 工厂」的三件套，你已经第三次见到了（Window / InputPoller / RenderContext）——这就是引擎的通用解法。

---

## 🔧 练习

1. 把仓库现有代码拆成 `Window`（瘦身后）+ `OpenGLContext`，确保红屏窗口行为不变。
2. 把 GLEW 换成你 `src/glad.c` 里的 GLAD：只改 `OpenGLContext.cpp`，`Window` 一行不改。体会解耦的回报。
3. （进阶）实现一个 `NullContext`（什么都不做的上下文），用于引擎在「无显卡」环境下的逻辑测试。

## 📝 参考答案

### 1. 拆成 `Window` + `OpenGLContext`，红屏行为不变

`OpenGLContext` 照 4.4 节写。`GLFWWindowImpl::init` 瘦身（删掉 `glewInit`/`glfwMakeContextCurrent`）：

```cpp
bool GLFWWindowImpl::init(const Props& p) {
    // ... glfwInit / glfwCreateWindow / 注册回调（同基础篇 04）
    m_context = RenderContext::create((void*)m_window);
    m_context->init((void*)m_window);
    m_context->setVsync(p.vsync);
    return true;
}
void GLFWWindowImpl::swapBuffers()        { m_context->swapBuffers(); }
void GLFWWindowImpl::setVsync(bool v)     { m_context->setVsync(v); }
void GLFWWindowImpl::shutdown() {
    m_context.reset();              // 先释放 GL 资源（见 4.6）
    if (m_window) { glfwDestroyWindow(m_window); m_window = nullptr; }
}
```

`main.cpp` 一字不改，红屏依旧。

### 2. GLEW → GLAD（只改 `OpenGLContext.cpp`）

```cpp
#include <glad/gl.h>            // 替换 <GL/glew.h>
#include <GLFW/glfw3.h>

void OpenGLContext::init(void*) {
    glfwMakeContextCurrent(m_window);
    static bool inited = false;
    if (!inited) {
        int v = gladLoaderLoadGL(glfwGetProcAddress);   // GLAD 用 GLFW 的 proc
        if (v == 0) { std::cerr << "Failed to init GLAD\n"; return; }
        inited = true;
    }
    std::cout << "OpenGL " << glGetString(GL_VERSION) << "\n";
}
```

`Window` 零改动——这就是解耦的回报（GLEW/GLAD 只活在 context 的 `.cpp` 里）。

### 3.（进阶）`NullContext`（无显卡环境测试用）

```cpp
class NullContext : public RenderContext {
    bool m_vsync{true};
public:
    void init(void*) override {}                 // 全空实现
    void swapBuffers() override {}
    void setVsync(bool v) override { m_vsync = v; }
    bool isVsync() const override { return m_vsync; }
};
// 工厂里：#if defined(RENDER_API_NULL) return std::make_unique<NullContext>();
```

配合 `HeadlessWindow`，引擎逻辑能在无 GPU 环境跑起来。

---

下一篇 👉 [05 集成 ImGui](05-集成ImGui.md)
