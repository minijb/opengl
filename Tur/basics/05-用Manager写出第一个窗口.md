# 05 · 用 Manager 写出第一个窗口

> 目标：把 [04 篇](04-实现基础GLFWManager.md) 的 `GLFWManager` 接进 `main.cpp`，
> 跑出你的第一个「引擎式」窗口，并学会排错。

---

## 5.1 改写 `src/main.cpp`

📦 完整内容，可直接覆盖 `src/main.cpp`：

```cpp
#include "GLFWManager.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>   // 只为 GLFW_KEY_* / GLFW_PRESS 这些常量；业务逻辑不直接调 glfw*
#include <iostream>

int main() {
    GLFWManager window;
    if (!window.init({ "Chapter 2 - program 1", 600, 600 })) {
        std::cerr << "Window init failed\n";
        return -1;
    }

    // 初始 viewport（用 Manager 的尺寸，而不是硬编码 600）
    glViewport(0, 0, window.width(), window.height());

    // —— 订阅事件：业务逻辑与窗口库彻底解耦 ——
    window.setResizeCallback([](int w, int h) {
        glViewport(0, 0, w, h);                 // 窗口缩放时，画布跟着变
    });
    window.setKeyCallback([&](int key, int, int action, int) {
        if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
            window.setShouldClose(true);        // 按 ESC 关窗
    });

    // —— 主循环：一个 glfw 调用都看不到 ——
    while (!window.shouldClose()) {
        glClearColor(1.0f, 0.0f, 0.0f, 1.0f);   // 红色背景
        glClear(GL_COLOR_BUFFER_BIT);

        // 轮询式输入示例（取消注释试试）：
        // if (window.isKeyPressed(GLFW_KEY_A)) { /* 向左移动 */ }

        window.swapBuffers();
        window.pollEvents();
    }
    // window 的析构函数自动 shutdown，不用手写清理
    return 0;
}
```

### 和原版 `main.cpp` 的对比

| 原版 | 现在 |
|---|---|
| `glfwInit()` / `glfwTerminate()` 散在 `main` | 全部在 `GLFWManager` 内部 |
| `exit(EXIT_FAILURE)` 直接死 | `init()` 返回 `false`，自己决定 |
| 输入要手写 `glfwGetKey(window, ...)` | `window.isKeyPressed(...)` 或事件回调 |
| 改个尺寸到处是 `600` 魔数 | `window.width()` 跟着窗口走 |
| 忘了 `glfwDestroyWindow` 就泄漏 | RAII 析构自动清理 |

---

## 5.2 编译运行

你的 `CMakeLists.txt` 用 `file(GLOB_RECURSE src/*.cpp *.h *.c)`，新文件**自动**入编译，不用改 CMake。

```bash
cmake -B build
cmake --build build --config Debug
# Windows: ./build/Debug/MyOpengl.exe
# Linux/Mac: ./build/MyOpengl
```

**预期结果**：一个 600×600 的红色窗口，标题 `Chapter 2 - program 1`，按 ESC 关闭，可以拖拽改变大小且画面跟着缩放（不撕裂、不黑边）。

---

## 5.3 验证清单（逐项确认）

跑起来后一项项过：

- [ ] 窗口出现，背景纯红（`glClearColor(1,0,0,1)` 生效）。
- [ ] 按 **ESC**，窗口正常关闭，程序退出码 0。
- [ ] 拖拽窗口边框改变大小，红色始终铺满（说明 `ResizeCallback` → `glViewport` 生效）。
- [ ] 关闭窗口后，控制台**没有**报 GL/GLFW 错误（说明析构清理干净）。
- [ ] 把 `setVsync` 关掉（`window.init({ "...", 600, 600, false, false })`，最后一个参数 `vsync=false`），
      观察 CPU 占用升高——证明 VSync 开关真的起作用。
- [ ] 临时在 `setKeyCallback` 里加 `std::cout << key;`，按键时打印键码——证明事件回调真的在跑。

---

## 5.4 常见错误与排错

### ❌ 编译报 `GLFWManager.h: No such file or directory`

文件名拼写问题（[04 篇 4.0](04-实现基础GLFWManager.md)）。确认头文件叫 `GLFWManager.h`（2 个 a），不是 `Manaager`。

### ❌ 编译报 `incomplete type GLFWwindow`

你在头文件里写了 `GLFWwindow m_window;`（值成员）而不是指针。前向声明的类型只能用指针/引用，
改成 `GLFWwindow* m_window;`。

### ❌ 链接报 `undefined reference to GLFWManager::init()`

`.cpp` 没被编译。确认 `src/GLFWManager.cpp` 存在，且重新 `cmake -B build` 让 GLOB 重新扫描。
**改文件名后必须重新 configure**（删掉 `build/` 再来一遍最保险）。

### ❌ 运行一闪而过 / 直接闪退

多半是 `init()` 返回 `false`。看 `std::cerr` 输出，常见原因：
- 显卡不支持 OpenGL 4.1 → 把 hint 的 `MAJOR/MINOR` 降到 `3,3`。
- GLEW 初始化失败 → 确认 `glewExperimental = GL_TRUE;` 在 `glewInit()` 之前。

### ❌ 按 ESC 没反应

`setKeyCallback` 里的 lambda 用了 `[&]` 捕获 `window`，但回调注册的时机要确认在 `init()` 之后。
如果你在 `init` 之前注册，那时 `m_window` 还是 `nullptr`，回调虽然存了但 GLFW 还没绑定——
**先 `init`，再 `setXxxCallback`**，顺序别反。

### ❌ 改变窗口大小后画面错位 / 留黑边

`framebufferSizeCallback` 里漏了同步 `m_width/m_height` 或漏了 `glViewport`。
两者都要做：Manager 内部同步尺寸，业务层的 `ResizeCallback` 调 `glViewport`。

### ❌ macOS 上窗口透明 / 不渲染

忘了 `glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);`。`.cpp` 里有 `#ifdef __APPLE__` 守卫，确认它在。

---

## 5.5 小结

恭喜，你现在拥有一个**真正解耦的窗口子系统**：

- `main.cpp` 里除了 `GLFW_KEY_*` 常量，没有任何 `glfw` 函数调用。
- 想换 GLFW 版本、甚至换窗口库，改动都局限在 `GLFWManager.cpp` 内部。
- 错误可控、资源自动释放、输入和事件都有标准入口。

基础篇到此结束。你已经具备了「引擎式」思维。进阶篇会把它推向真正的引擎级：
事件系统、接口抽象、ImGui 调试面板、多窗口……

---

## 🔧 练习（基础篇综合）

1. 给 `GLFWManager` 加一个 `setTitle(const std::string&)` 方法，并在 `main` 里按 `T` 键切换标题为当前帧数（提示：用一个全局计数器）。
2. 加一个鼠标位置事件：定义 `MouseMovedCallback`，注册 `glfwSetCursorPosCallback`，在 `main` 里打印鼠标坐标。
3. 把背景色做成随时间渐变：在循环里用 `glfwGetTime()`（或自己计时）算 RGB，调 `glClearColor`。体会「Manager 把窗口管好后，你只需专注渲染逻辑」。

## 📝 参考答案

> 基础篇综合题，下面是参考实现。

### 1. `setTitle` + 按 T 切换标题为帧数

头加（同 [04 篇练习 2](04-实现基础GLFWManager.md)）：`void setTitle(const std::string&);`

```cpp
int main() {
    GLFWManager window;
    window.init({ "Chapter 2 - program 1", 600, 600 });
    glViewport(0, 0, window.width(), window.height());

    long long frame = 0;
    window.setKeyCallback([&](int key, int, int action, int) {
        if (key == GLFW_KEY_T && action == GLFW_PRESS)
            window.setTitle("Frame: " + std::to_string(frame));   // 只在按下时切
        if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
            window.setShouldClose(true);
    });

    while (!window.shouldClose()) {
        glClearColor(1, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ++frame;
        window.swapBuffers();
        window.pollEvents();
    }
}
```

### 2. 鼠标位置事件

```cpp
// 头里加类型 + 成员 + setter + 静态回调
using MouseMovedCallback = std::function<void(double x, double y)>;
MouseMovedCallback m_mouseMoveCb;
void setMouseMoveCallback(MouseMovedCallback cb) { m_mouseMoveCb = std::move(cb); }
static void cursorPosCallback(GLFWwindow* w, double x, double y);

// init() 里注册：
glfwSetCursorPosCallback(m_window, cursorPosCallback);

// 实现文件：
void GLFWManager::cursorPosCallback(GLFWwindow* w, double x, double y) {
    auto* self = static_cast<GLFWManager*>(glfwGetWindowUserPointer(w));
    if (self && self->m_mouseMoveCb) self->m_mouseMoveCb(x, y);
}

// main 里：
window.setMouseMoveCallback([](double x, double y){
    std::cout << "mouse: " << x << ", " << y << "\n";
});
```

### 3. 背景色随时间渐变

```cpp
while (!window.shouldClose()) {
    float t = (float)glfwGetTime();
    // 用正弦让 RGB 在 [0,1] 之间平滑循环
    glClearColor(0.5f + 0.5f * std::sin(t),
                 0.5f + 0.5f * std::sin(t + 2.094f),   // +120°
                 0.5f + 0.5f * std::sin(t + 4.188f),   // +240°
                 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    window.swapBuffers();
    window.pollEvents();
}
```

体会：窗口细节都在 Manager 里，渲染循环只剩「算颜色」这一件事。

---

进阶篇开始 👉 [进阶篇 01 事件系统设计](../advanced/01-事件系统设计.md)
