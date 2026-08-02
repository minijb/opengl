# 进阶 05 · 集成 ImGui

> 目标：还记得基础篇里那个 `nativeHandle()`「逃生舱口」吗？我们一直说「业务代码别用，给 ImGui 留的」。
> 这一篇兑现承诺——把 ImGui 接进引擎，做出调试面板。同时讲清楚 ImGui 和游戏如何共享事件、共享渲染。

---

## 5.1 为什么引擎都需要 ImGui

游戏引擎开发 80% 的时间在「调参」：摄像机速度、光照强度、物理重力、AI 视野角……
没有即时 UI 时，改一个数要重新编译、重启、跑到那个场景——慢得想哭。

**Dear ImGui** 是即时模式（immediate mode）GUI：每帧你用代码「描述」这一帧的界面长什么样，
它自动处理输入、绘制。适合：

- 实时调参滑块（摄像机 fov、清屏颜色、物体位置）。
- 性能监视器（帧率、draw call 数）。
- 实体检视器（选中物体，改它的 transform）。
- 调试输出（日志、状态变量）。

> 💡 「即时模式」 vs 「保留模式」（Qt、WinForms）：保留模式你创建控件、它一直存在；
> 即时模式你每帧重画一遍，控件不存在「持久状态」，代码和数据天然同步。

---

## 5.2 ImGui 在引擎里要的三个东西

1. **原生窗口句柄**：ImGui 需要知道画到哪个窗口、从哪个窗口收输入 → 这就是 `nativeHandle()` 的用途。
2. **GL 上下文**：ImGui 用 GL 画图 → 上下文已 current（`RenderContext::init` 做了）。
3. **事件**：鼠标键盘要喂给 ImGui，且 ImGui「想用」时要拦截游戏 → 接 [01 篇](01-事件系统设计.md) 的事件系统。

---

## 5.3 接入步骤

### 第 1 步：拿到 ImGui 源码

ImGui 是单文件库，放进 `lib/imgui/`（和你的 glfw/glew 并列），CMake 加进编译：

```cmake
add_library(imgui STATIC
    lib/imgui/imgui.cpp
    lib/imgui/imgui_draw.cpp
    lib/imgui/imgui_tables.cpp
    lib/imgui/imgui_widgets.cpp
    lib/imgui/backends/imgui_impl_glfw.cpp
    lib/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC lib/imgui lib/imgui/backends)
target_link_libraries(imgui glfw ${OPENGL_LIB})   # backend 依赖 glfw + gl
target_link_libraries(MyOpengl imgui)
```

### 第 2 步：初始化

```cpp
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

class ImGuiLayer {
public:
    void onAttach(GLFWwindow* nativeWindow) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        // io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;   // 想要 dock 布局就开

        ImGui_ImplGlfw_InitForOpenGL(nativeWindow, true);   // ← nativeHandle() 在这用
        ImGui_ImplOpenGL3_Init("#version 410");              // 要和你的 GL 版本一致
    }

    void onDetach() {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
    // ...
};
```

> ⚠️ `#version 410` 必须和你 `glfwWindowHint` 设的 GL 版本匹配。你设的 4.1，所以写 `410`。

注意第二个参数：`ImGui_ImplGlfw_InitForOpenGL(window, true)` 的 `true` 表示**让 ImGui 自己安装 GLFW 回调**。
但你已经自己装了回调（[基础篇 04](../basics/04-实现基础GLFWManager.md)）！两种处理：

- 传 `true`：ImGui 会在你的回调**链上**追加自己的（GLFW 支持回调链），你的事件照常收。
- 传 `false`：你手动在每次 `pollEvents` 后调 `ImGui_ImplGlfw_...` 喂事件。

推荐 `true`——省事，且 ImGui 的 GLFW backend 内部会用 `glfwSet*Callback` 的返回值把前一个回调链接起来。

### 第 3 步：每帧三段式

```cpp
// 每帧：
void ImGuiLayer::begin() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();      // 处理输入、窗口尺寸
    ImGui::NewFrame();
}

void ImGuiLayer::end() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());   // 画到当前 framebuffer
}
```

主循环里：

```cpp
while (!window.shouldClose()) {
    imgui.begin();

    // —— 你的 ImGui 面板 ——
    ImGui::Begin("Debug");
    ImGui::Text("FPS: %.1f", fps);
    ImGui::ColorEdit3("Clear Color", clearColor);   // 拖滑块实时改清屏色
    ImGui::SliderFloat("Camera FOV", &fov, 30, 120);
    if (ImGui::Button("Reset")) resetCamera();
    ImGui::End();

    // —— 游戏渲染 ——
    glClearColor(clearColor[0], clearColor[1], clearColor[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    renderScene();

    imgui.end();                    // ImGui 画在最上层
    window.swapBuffers();
    window.pollEvents();
}
```

拖动 `Clear Color` 滑块，背景色立刻变——这就是即时模式调参的爽感。

---

## 5.4 事件共享：ImGui 和游戏互不抢输入

这是最关键的工程问题。当鼠标在 ImGui 面板上点击时，**游戏不应该同时响应**（否则点滑块也会开枪）。

ImGui 通过 `ImGuiIO` 的这几个标志告诉你它的需求：

```cpp
ImGuiIO& io = ImGui::GetIO();
io.WantCaptureMouse    // ImGui 想要鼠标（鼠标在面板上）
io.WantCaptureKeyboard // ImGui 想要键盘（某个输入框聚焦了）
io.WantTextInput       // 想要文本输入
```

在事件层拦截（[01 篇 1.6 节](01-事件系统设计.md) 的 `handled` 机制派上用场）：

```cpp
m_window->setEventCallback([](Event& e){
    ImGuiIO& io = ImGui::GetIO();
    e.handled |= e.isInCategory(EventCategory::Mouse)    && io.WantCaptureMouse;
    e.handled |= e.isInCategory(EventCategory::Keyboard) && io.WantCaptureKeyboard;
});

// 然后业务层只处理「没被标记 handled」的事件
```

或者在轮询层判（[03 篇](03-输入系统.md)）：

```cpp
if (!ImGui::GetIO().WantCaptureMouse) {
    if (Input::isMouseButtonPressed(MOUSE_LEFT)) shoot();
}
```

两种都行，事件层拦截更彻底（连「按下」瞬间事件都不给游戏）。

---

## 5.5 用 Layer 把 ImGui 整进引擎架构

真实引擎里 ImGui 不是一个全局函数集合，而是做成一个 **Layer**（层），挂到引擎的层栈里：

```cpp
class Layer {
public:
    virtual void onAttach() {}
    virtual void onDetach() {}
    virtual void onUpdate() {}
    virtual void onEvent(Event& e) {}
    virtual void onImGuiRender() {}
};

class ImGuiLayer : public Layer {
    void onAttach() override { /* 上面第 2 步的 init */ }
    void onDetach() override { /* shutdown */ }
    void onImGuiRender() override { /* 你不需要在这画，end() 里统一 Render */ }
    void begin();   // 主循环开头调
    void end();     // 主循环末尾调
};
```

引擎主循环遍历层栈：

```
for (layer : layers) layer->onUpdate();        // 游戏逻辑
for (layer : layers) layer->onImGuiRender();   // 各层画自己的调试 UI
imguiLayer->end();                             // 统一提交渲染
```

这是 Hazel / Unreal 的「Application + LayerStack」架构。ImGui 只是其中一层，你的游戏逻辑是另一层。

---

## 5.6 排错清单

| 现象 | 原因 |
|---|---|
| 编译报 `imgui_impl_glfw.h: No such file` | 没把 `backends/` 加进 include 目录 |
| ImGui 画不出来 / 全黑 | `begin()/end()` 顺序错，或 `end()` 在 `swapBuffers` 之后 |
| 点击 ImGui 时游戏也响应 | 没处理 `WantCaptureMouse`（见 5.4） |
| 字体是方块 | 没加载中文字体：`io.Fonts->AddFontFromFileTTF("...", 18, nullptr, io.Fonts->GetGlyphRangesChineseFull())` |
| 鼠标位置偏移 | HiDPI 屏没处理：`glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE)` |
| `#version` 报错 | `ImGui_ImplOpenGL3_Init` 的版本串和 `glfwWindowHint` 不一致 |

---

## 5.7 小结

- ImGui 是引擎调试的「倍速器」，靠 `nativeHandle()` + 已 current 的上下文 + 事件系统三件套接入。
- 每帧 `begin()`（NewFrame）→ 画面板 → `end()`（RenderDrawData），画在游戏画面上层。
- **事件共享是核心**：用 `io.WantCaptureMouse/Keyboard` + `Event::handled` 让 ImGui 和游戏互不抢输入。
- 工程上把 ImGui 封装成一个 `Layer`，挂进 `LayerStack`，和游戏逻辑层并列。

到这里，你的引擎已经有了：窗口 + 事件 + 输入 + 上下文 + 调试 UI。接下来讲多窗口、整体生命周期、性能与测试。

---

## 🔧 练习

1. 接入 ImGui，做一个面板：实时显示鼠标坐标、当前帧率、一个清屏颜色滑块（拖动改变背景）。
2. 加一个「实体检视器」面板：列出几个虚拟物体的名字，点击选中后显示它的位置 XYZ 滑块。
3. （进阶）开启 `DockingEnable`，做一个可停靠的多面板布局（场景树 + 检视器 + 控制台）。

## 📝 参考答案

### 1. 调试面板（鼠标坐标 / 帧率 / 清屏色滑块）

```cpp
float fps = 0, clearColor[3] = {1, 0, 0};
auto [mx, my] = Input::getMousePosition();

ImGui::Begin("Debug");
ImGui::Text("Mouse: (%.1f, %.1f)", mx, my);
ImGui::Text("FPS: %.1f", fps);
ImGui::ColorEdit3("Clear Color", clearColor);
ImGui::End();

glClearColor(clearColor[0], clearColor[1], clearColor[2], 1.0f);  // 拖滑块实时变
```

### 2. 实体检视器

```cpp
struct Entity { const char* name; float pos[3]; bool selected = false; };
std::vector<Entity> ents = { {"Cube", {0,0,0}}, {"Light", {2,3,1}} };

ImGui::Begin("Inspector");
for (auto& e : ents) {
    if (ImGui::Selectable(e.name, e.selected))          // 点选
        for (auto& o : ents) o.selected = (&o == &e);   // 单选
}
auto* sel = std::find_if(ents.begin(), ents.end(), [](auto& e){ return e.selected; });
if (sel != ents.end()) {
    ImGui::Text("Editing: %s", sel->name);
    ImGui::SliderFloat3("Position", sel->pos, -10, 10);
}
ImGui::End();
```

### 3.（进阶）开启 DockingEnable 多面板

```cpp
ImGuiIO& io = ImGui::GetIO();
io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;   // onAttach 里开
// 主循环里画一个 dock 空间让停靠生效：
ImGui::DockSpaceOverViewport(ImGui::GetMainViewport());
// 然后正常 Begin("Scene") / Begin("Inspector") / Begin("Console")，拖标题栏停靠
```

---

下一篇 👉 [06 多窗口与生命周期管理](06-多窗口与生命周期管理.md)
