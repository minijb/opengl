# 03 · RAII 与 C++ 封装思想

> 目标：这一篇是「内功心法」。把资源管理、三五法则、前向声明讲透，
> 后面写 `GLFWManager` 时你会觉得「理所当然」。如果你 C++ 够扎实可以略读。

---

## 3.1 资源管理：为什么 C 程序员总在忘 `free`

GLFW 是 C 库，它的资源管理是手动配对的：

```cpp
glfwInit();              ┐  一对
glfwTerminate();         ┘

glfwCreateWindow();      ┐  一对
glfwDestroyWindow();     ┘
```

C 风格的典型 bug：

```cpp
GLFWwindow* w = glfwCreateWindow(...);
if (某个条件) {
    return;          // ← 忘了 glfwDestroyWindow(w)，泄漏！
}
glfwDestroyWindow(w);
```

更糟的是 C++ **异常**：

```cpp
GLFWwindow* w = glfwCreateWindow(...);
doSomethingRisky();     // 万一这里抛异常，下面的 destroy 永远执行不到
glfwDestroyWindow(w);   // ← 异常时跳过，泄漏！
```

只要资源获取和释放之间隔着任何「可能提前离开」的点（return / break / throw / continue），配对就会被打破。

---

## 3.2 RAII：让对象的生命周期 == 资源的生命周期

**RAII**（Resource Acquisition Is Initialization）：把资源的获取绑到对象的**构造**，释放绑到对象的**析构**。
对象活着，资源就在；对象死亡（无论怎么死——正常离开作用域、抛异常、`return`），析构函数自动跑，资源自动释放。

应用到窗口上：

```cpp
class GLFWManager {
public:
    GLFWManager()  { /* 构造时建窗 */ }
    ~GLFWManager() { /* 析构时销毁窗 */ }
};

void f() {
    GLFWManager window;        // 构造 → 建窗
    doSomethingRisky();        // 抛异常也没关系
}                              // 离开作用域 → 自动销毁窗 ✅
```

C++ 保证：**栈上对象在离开作用域时一定会析构**，即使是因异常展开（stack unwinding）。
这一条语言保证，让你不用再操心「中间 return 怎么办」「异常怎么办」。

> 💡 这是 C++ 相对 C / Java（try-finally）/ Go（defer）最优雅的资源管理方式，
> 也是 STL 容器、智能指针、锁守卫（`std::lock_guard`）背后的统一思想。

---

## 3.3 两段式构造：为什么不都在构造函数里建窗

理论上 RAII 要「构造即就绪」，但 GLFW 初始化可能失败（显卡不支持、驱动太老……）。
如果失败抛异常，析构要处理「半初始化」状态，代码变复杂。

**两段式构造**（two-phase init）是实战中常用的折中：

```cpp
class GLFWManager {
public:
    GLFWManager();        // 空构造，什么都不做
    bool init(...);       // 显式初始化，返回是否成功
    ~GLFWManager();       // 析构里判断「是否已初始化」再清理
};
```

调用方：

```cpp
GLFWManager window;
if (!window.init({ "Demo", 800, 600 })) {
    // 失败处理：日志、回退、退出……你自己决定
    return -1;
}
```

好处：
- **错误处理权交还调用方**（不再 `exit(EXIT_FAILURE)`）。
- 析构函数只需 `if (m_window) glfwDestroyWindow(m_window);`，简单安全。

这也是 [04 篇](04-实现基础GLFWManager.md) 采用的形式。

---

## 3.4 三五法则（Rule of Three / Five）

当你手写析构函数管理资源时，编译器默认生成的拷贝/赋值行为通常是错的。

```cpp
GLFWManager a;
a.init({});                // a 持有窗口 W1
GLFWManager b = a;         // ❌ 默认拷贝：b.m_window 也指向 W1
// a 析构 → 销毁 W1
// b 析构 → 又销毁 W1（已销毁）→ 崩溃/未定义行为
```

**三法则**：如果你需要自定义其中一个，通常三个都要：
- 析构函数
- 拷贝构造
- 拷贝赋值

**五法则**（C++11 起）再加两个：
- 移动构造
- 移动赋值

对 `GLFWManager`，窗口是**独占资源**，正确的处理是 **禁用拷贝**：

```cpp
class GLFWManager {
public:
    GLFWManager();
    ~GLFWManager();

    // 禁用拷贝（窗口是独占的，不能共享）
    GLFWManager(const GLFWManager&)            = delete;
    GLFWManager& operator=(const GLFWManager&) = delete;
};
```

> 💡 移动是否支持？可以，但要小心：移动后要把源对象的 `m_window` 置 `nullptr`，
> 否则两个析构都会销毁同一个窗口。本教程基础篇先不做移动，保持简单；进阶需要时再加。

**零法则（Rule of Zero）**：如果你能让所有成员都是 RAII 类型（智能指针、STL 容器），
就不用手写任何特殊成员函数。窗口是 C 裸指针，所以这里做不到零法则，只能手管 + 禁拷贝。

---

## 3.5 前向声明：别在头文件里 `#include <GLFW/glfw3.h>`

新手常犯：在 `GLFWManager.h` 顶部直接

```cpp
#include <GLFW/glfw3.h>   // ❌ 让所有 include 这个头的人都背上 GLFW 的编译成本
```

GLFW 的头（加上它拉的依赖）很大，会显著拖慢编译。更糟的是**耦合**：业务代码本来不该知道底层是 GLFW。

**正确做法：前向声明**

```cpp
// GLFWManager.h
#pragma once

struct GLFWwindow;          // ✅ 只告诉编译器“有这么个类型”，不拉它的头

class GLFWManager {
    GLFWwindow* m_window;   // 指针/引用只需要前向声明就够
    // ...
};
```

在 `.cpp` 里才真正 include：

```cpp
// GLFWManager.cpp
#include "GLFWManager.h"
#include <GLFW/glfw3.h>     // 这里才需要完整定义，调用 glfwXXX
```

效果：
- `#include "GLFWManager.h"` 的文件**不会**拉 GLFW 头 → 编译快、不耦合。
- 只有实现文件 `GLFWManager.cpp` 依赖 GLFW → 换库时改动局限在一个文件。

> ⚠️ 前向声明只能用于「指针 / 引用」成员。如果你要在头里写 `GLFWwindow m_window;`（值成员），
> 就必须 include 完整定义。所以窗口句柄一律用指针。

---

## 3.6 Pimpl 惯用法（进阶预告）

前向声明的极致版本是 **Pimpl**（pointer to implementation）：把所有私有成员藏到一个单独的 impl 类里，
主类只持有一个指向它的 `std::unique_ptr`。

```cpp
class GLFWManager {
public:
    GLFWManager();
    ~GLFWManager();          // 注意：析构要在 .cpp 里定义（unique_ptr 需要完整类型）
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;   // 所有私有细节都在 Impl 里
};
```

好处：ABI 稳定（改私有成员不破坏二进制兼容）、编译依赖最小。
本教程基础篇用前向声明就够了；Pimpl 留作 [进阶篇 02](../advanced/02-窗口抽象层与接口设计.md) 的练习。

---

## 3.7 这一篇和 Manager 的对应关系

| 本篇概念 | 在 GLFWManager 里体现为 |
|---|---|
| RAII | 构造/析构管窗口生命周期 |
| 两段式构造 | `init()` 返回 `bool` |
| 三五法则 | `= delete` 禁用拷贝 |
| 前向声明 | 头里只 `struct GLFWwindow;` |

把这些内化，下一篇的代码你会读得非常顺。

---

## 🔧 练习

1. 以下代码有什么资源安全问题？用 RAII 重写：
   ```cpp
   void f() {
       GLFWwindow* w = glfwCreateWindow(...);
       loadAssets();     // 可能抛异常
       render(w);
       glfwDestroyWindow(w);
   }
   ```
2. 解释：为什么 `GLFWManager b = a;`（默认拷贝）会出问题？禁用拷贝后，用户硬要这么做会怎样？
3. （进阶）查资料：`std::unique_ptr<Impl>` 的析构为什么必须在 `.cpp` 里定义，不能 `= default` 在头里？

## 📝 参考答案

### 1. 资源安全问题 + RAII 重写

问题：`loadAssets()` 若抛异常，`glfwDestroyWindow(w)` 永不执行 → 窗口句柄泄漏（更糟的是 `glfwInit` 的进程级资源也无处回收）。RAII 重写：

```cpp
class ScopedWindow {
public:
    explicit ScopedWindow(GLFWwindow* w) : m_w(w) {}
    ~ScopedWindow() { if (m_w) glfwDestroyWindow(m_w); }  // 异常展开时也跑
    ScopedWindow(const ScopedWindow&) = delete;
    ScopedWindow& operator=(const ScopedWindow&) = delete;
    operator GLFWwindow*() const { return m_w; }
private:
    GLFWwindow* m_w;
};

void f() {
    ScopedWindow w(glfwCreateWindow(...));
    if (!w) return;
    loadAssets();   // 抛异常也安全：离开作用域自动 destroy
    render(w);
}
```

### 2. 为什么默认拷贝出问题？禁用后硬拷贝会怎样？

默认拷贝是**浅拷贝**：`b.m_window = a.m_window`（两个对象持有同一裸指针）。析构 `a` → `glfwDestroyWindow(W1)`；析构 `b` → 再 `glfwDestroyWindow(W1)`（已销毁）→ **double-free / UB / 崩溃**。

禁用拷贝（`= delete`）后，用户硬写 `GLFWManager b = a;` 会**编译期报错**（deleted function），错误被挡在编译阶段而不是运行期崩溃——这正是禁拷贝的目的。

### 3.（进阶）`unique_ptr<Impl>` 析构为何必须在 `.cpp` 定义

`std::unique_ptr<T>` 的析构需要调用 `T` 的析构函数，因此**删除器需要 `T` 的完整类型定义**。在头文件 `= default` 时，`Impl` 还是前向声明（不完整类型），编译器无法生成调用 `~Impl()` 的代码 → 编译错误。所以约定：头里只声明 `~GLFWManager();`，在 `.cpp` 里 `#include` 完整 `Impl` 定义后再 `GLFWManager::~GLFWManager() = default;`，此时 `unique_ptr<Impl>` 能看到完整 `Impl`，析构链成立。这就是 Pimpl 的「代价与纪律」。

---

下一篇 👉 [04 实现基础 GLFWManager](04-实现基础GLFWManager.md) —— 终于开始写代码了。
