# Extra · Signal/Slot 信号槽完全指南

> 目标：[事件系统架构全景](事件系统架构全景-七种模式对比.md) 把 Signal/Slot 列为模式 ②，只给了速览。
> 这篇把它讲透：从一个能跑的迷你实现（约 100 行）到工业标准 Boost.Signals2 的全部核心能力
> （连接管理、自动断开、分组、combiner、线程安全），再到五个主流库的选型。
> 练习全部附参考答案，其中纯 C++ 部分的代码均在本仓库环境（MSVC 19.50 / C++17 / `/W4`）下编译运行通过。

---

## 1. 先分清两个 "Signal"

C++ 世界里有两个都叫「信号」的东西，八竿子打不着：

| | Signal/Slot（本篇） | OS 进程信号（姊妹篇） |
|---|---|---|
| 是什么 | **库级别的观察者模式**：一个事件源通知 N 个订阅者 | **操作系统级别的软件中断**：内核通知进程"出事了" |
| 关键字 | `signal`、`slot`、`connect`、`emit` | `SIGINT`、`SIGSEGV`、`signal()`、`sigaction` |
| 代表 | Qt、Boost.Signals2、EnTT | `<csignal>`、POSIX |
| 典型问题 | 「纹理加载完了，谁关心谁订阅」 | 「用户按了 Ctrl+C，程序怎么优雅退出」 |

另一篇见 [OS 信号处理 signal 完全指南](OS信号处理signal完全指南.md)。本篇只讲前者。

---

## 2. 为什么单个 `std::function` 回调不够用

你的 `GLFWManager`（[基础篇 04](../basics/04-实现基础GLFWManager.md)）现在是这样的：

```cpp
using KeyCallback = std::function<void(int key, int scancode, int action, int mods)>;
KeyCallback m_keyCb;

void setKeyCallback(KeyCallback cb) { m_keyCb = std::move(cb); }
```

它是 **1 对 1** 的：后注册的覆盖先注册的。只要出现下面任何一个需求，它就不够了：

| 需求 | 单 `std::function` 的窘境 |
|---|---|
| ImGui 要吃键盘事件，游戏输入也要吃 | 两者只能活一个（互相覆盖） |
| 音频系统想听"纹理加载完成"，渲染也想听 | 你得手写"二选一转发"的胶水 |
| 某个订阅者生命周期结束，要取消订阅 | `std::function` 没有句柄，没法精确摘掉 |
| 调试期临时挂一个"打印所有事件"的探针 | 又得覆盖正式回调 |

**Signal 就是为这些场景生的：把「一个回调槽」升级成「一个可增删的回调列表」，并管理好它们的连接与生命周期。**

---

## 3. 概念与术语

```
   发布者 (Publisher)                 订阅者 (Subscribers)
┌──────────────────┐   emit(655, 0, 1, 0)
│  GLFWManager     │──────────────► ┌─────────┐ ┌─────────┐ ┌─────────┐
│                  │               │ 槽 slot1 │ │ 槽 slot2 │ │ 槽 slot3 │
│  Signal<int,...> │               │ ImGui   │ │ Input  │ │ DebugLog│
│   ┌──────────┐   │               └────▲────┘ └────▲────┘ └────▲────┘
│   │ 槽列表    │───┼─── connection ────┘           │            │
│   └──────────┘   │   (连接，可断开)               │            │
└──────────────────┘                               │            │
```

| 术语 | 一句话 | 你已知的对应物 |
|---|---|---|
| **Signal 信号** | 一个具名的多播事件源 | `EventCallback` 的"多播版" |
| **Slot 槽** | 订阅时可传入的任意可调用对象 | lambda、`std::function` 目标 |
| **Connect 连接** | 把槽挂到信号上，得到一个连接句柄 | `setKeyCallback(...)` 的调用本身 |
| **Disconnect 断开** | 用句柄精确摘掉一个槽 | ❌ `std::function` 做不到 |
| **Emit / Fire 发布** | 按顺序调用所有活着的槽 | `m_eventCb(e)` 的多播版 |

> 💡 与 [进阶 01](../advanced/01-事件系统设计.md) 的 Event + Dispatcher（模式 ①）互补：
> ① 是「**一条统一管道**，按事件类型分流」；② 是「**每个通知点一个独立信号**，天然多播」。

---

## 4. 30 秒原型（以及它为什么不能用）

最小可用的念头只需 4 行：

```cpp
template <typename... Args>
class SignalV0 {
    std::vector<std::function<void(Args...)>> m_slots;
public:
    void connect(std::function<void(Args...)> s) { m_slots.push_back(std::move(s)); }
    void emit(Args... args) { for (auto& s : m_slots) s(args...); }
};
```

多播有了，但三个致命问题让它在引擎里不可用：

1. **断不开**——`std::function` 没有相等比较，你无法精确摘掉"ImGui 那个槽"。
2. **emit 期间改列表 = UB**——某个槽里 `connect`（`push_back` 可能搬移）或析构了别的订阅者，正在进行的 range-for 迭代器直接悬垂。
3. **生命周期无人管**——订阅者析构后，槽里捕获的引用悬垂，下次 emit 崩溃。

下面三节，就是把这三件事逐一做对的约 100 行。

---

## 5. MiniSignal：一个能用的迷你实现（C++17）

📦 放进 `src/mini_signal.hpp` 即可用（本仓库 CMake 的 `GLOB` 会自动收录）。

```cpp
#pragma once
// mini_signal.hpp —— 教程用迷你 Signal/Slot 实现（C++17，无依赖）
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace mini {

template <typename... Args>
class Signal;

// RAII 连接守卫：析构时自动 disconnect（等价 boost::signals2::scoped_connection）
class ScopedConnection {
public:
    ScopedConnection() = default;

    template <typename... Args>
    ScopedConnection(Signal<Args...>& signal, std::uint64_t id)
        : m_disconnect([&signal, id] { signal.disconnect(id); }) {}

    ~ScopedConnection() {
        if (m_disconnect) m_disconnect();
    }

    ScopedConnection(const ScopedConnection&) = delete;
    ScopedConnection& operator=(const ScopedConnection&) = delete;

    ScopedConnection(ScopedConnection&& other) noexcept
        : m_disconnect(std::exchange(other.m_disconnect, nullptr)) {}

    ScopedConnection& operator=(ScopedConnection&& other) noexcept {
        if (this != &other) {
            if (m_disconnect) m_disconnect();
            m_disconnect = std::exchange(other.m_disconnect, nullptr);
        }
        return *this;
    }

    void disconnect() {
        if (m_disconnect) {
            m_disconnect();
            m_disconnect = nullptr;
        }
    }

    bool connected() const { return static_cast<bool>(m_disconnect); }

private:
    std::function<void()> m_disconnect;  // 惰性绑定 signal.disconnect(id)
};

template <typename... Args>
class Signal {
public:
    using Slot = std::function<void(Args...)>;  // 槽：任意可调用对象
    using Id   = std::uint64_t;                 // 连接句柄

    // 连接一个槽，返回可用于断开的句柄
    Id connect(Slot slot) {
        m_slots.push_back(std::make_unique<Record>(++m_nextId, std::move(slot)));
        return m_nextId;
    }

    // 连接并返回 RAII 守卫（守卫析构 = 自动断开）
    ScopedConnection connectScoped(Slot slot) {
        return ScopedConnection(*this, connect(std::move(slot)));
    }

    // 按句柄断开。emit 过程中调用是安全的（延迟清理）。
    void disconnect(Id id) {
        for (auto& r : m_slots)
            if (r && r->id == id) r->alive = false;   // 只标记，不立刻删除
        if (m_emitting == 0) sweep();
    }

    // 断开所有槽
    void disconnectAll() {
        for (auto& r : m_slots) r->alive = false;
        if (m_emitting == 0) sweep();
    }

    std::size_t slotCount() const {
        // 只统计还活着的槽
        std::size_t n = 0;
        for (const auto& r : m_slots)
            if (r && r->alive) ++n;
        return n;
    }

    // 发布：按连接顺序逐个调用活着的槽
    void emit(Args... args) {
        ++m_emitting;
        const std::size_t n = m_slots.size();  // 本次派发的快照
        for (std::size_t i = 0; i < n; ++i) {
            Record& r = *m_slots[i];
            if (r.alive) r.slot(args...);      // 槽内部 disconnect/connect 都不会失效
        }
        --m_emitting;
        if (m_emitting == 0) sweep();          // 最外层 emit 结束后才真正删除
    }

private:
    struct Record {
        Id id;
        Slot slot;
        bool alive = true;
        Record(Id i, Slot s) : id(i), slot(std::move(s)) {}
    };

    void sweep() {
        m_slots.erase(
            std::remove_if(m_slots.begin(), m_slots.end(),
                           [](const std::unique_ptr<Record>& r) {
                               return r && !r->alive;
                           }),
            m_slots.end());
    }

    std::vector<std::unique_ptr<Record>> m_slots;
    Id m_nextId = 0;
    int m_emitting = 0;  // >0 表示正在 emit（含递归 emit）
};

}  // namespace mini
```

### 5.1 三个设计点

| 设计 | 解决的问题 | 原理 |
|---|---|---|
| **标记删除 + 延迟 sweep** | v0 问题 2 | `disconnect` 只把 `alive` 置 false；真正的删除推迟到最外层 emit 结束。emit 期间列表结构永远稳定 |
| **`vector<unique_ptr<Record>>` 而非 `vector<Record>`** | 同上 | 槽里 `connect` 触发 `push_back` 扩容时，搬走的只是指针，`Record` 对象原地不动，按下标访问永远有效 |
| **自增句柄 `Id`** | v0 问题 1 | 每次连接发一个永不复用的编号，断开按编号标记，天然幂等（重复 disconnect 无害） |

> 💡 为什么 emit 开头要存 `const std::size_t n = m_slots.size()`？
> 这是本次派发的**快照**：emit 期间新 connect 的槽**本次不调用**（下轮才生效）。
> Boost.Signals2 对"派发中途新连接的槽"同样不保证在本轮生效——这是信号槽库的通用语义，不是偷懒。

### 5.2 语义保证表（实测）

以下每一条都在本仓库环境跑过测试验证：

| 场景 | 行为 |
|---|---|
| 多个槽按连接顺序调用 | ✅ 顺序稳定 |
| `disconnect(id)` 幂等 | ✅ 断两次无害 |
| 槽内自断开（一次性槽） | ✅ 本轮正常执行完，之后不再触发 |
| 槽 A 断开后面的槽 B | ✅ B 本轮就被跳过 |
| emit 期间新 connect | ✅ 本轮不调用，下一轮生效 |
| 递归 emit（槽里再 emit 自己） | ✅ 各层各自快照，sweep 推迟到最外层 |
| `ScopedConnection` 离开作用域 | ✅ 自动断开；移动后原守卫失效 |

对应输出（节选）：

```text
[3] ok: self-disconnect (one-shot), fired=1
[4] ok: disconnect other slot during emit
[5] ok: connect during emit deferred to next round
ALL MINI TESTS PASSED
```

### 5.3 MiniSignal 没做的事（诚实清单）

- ❌ **线程安全**——跨线程 connect/emit 需要加锁（见 §7.6 Boost 的做法）。
- ❌ **订阅者析构自动断开**——槽捕获 `this` 的对象析构后 emit 仍会悬垂。MiniSignal 靠 `ScopedConnection` 成员手动兜底；Boost 用 `track`（见 §7.4）。
- ❌ **返回值收集**——练习 3 带你手写。

---

## 6. ScopedConnection：连接的 RAII

最常用的断开时机是「订阅者自己析构的时候」。把守卫当成员一存，就再也不用手写 `disconnect`：

```cpp
class DebugProbe {           // 临时调试探针
    mini::ScopedConnection m_conn;
public:
    explicit DebugProbe(GLFWManager& window)
        : m_conn(window.keySignal.connectScoped([](int key, int, int action, int) {
              if (action == GLFW_PRESS) std::cout << "[probe] key " << key << '\n';
          })) {}
    // 析构 → m_conn 析构 → 自动断开，不碰窗口、不残留
};
```

> ⚠️ 生命周期规则：**守卫必须先于 Signal 析构**。守卫内部捕获了 signal 的指针，Signal 先死则守卫析构时解引用悬垂引用。成员顺序上把守卫放在「拥有 Signal 的对象」的订阅者一侧即可。

---

## 7. 工业标准：Boost.Signals2 全景

Boost.Signals2 是纯 ISO C++ 的信号槽库，**header-only**（无需链接），也是"该有的全都有"的参考实现。本节代码依据 [Boost.Signals2 官方教程](https://www.boost.org/doc/libs/latest/doc/html/signals2/tutorial.html)。

### 7.1 Hello, World 与多播

```cpp
#include <boost/signals2.hpp>

struct Hello { void operator()() const { std::cout << "Hello"; } };
struct World { void operator()() const { std::cout << ", World!\n"; } };

boost::signals2::signal<void()> sig;
sig.connect(Hello());
sig.connect(World());
sig();   // 输出：Hello, World!
```

信号对象本身可调用（`sig(5., 3.)`），参数会转发给每个槽。

### 7.2 调用顺序：分组

```cpp
boost::signals2::signal<void()> sig;
sig.connect(1, World());                        // 组 1
sig.connect(0, Hello());                        // 组 0：先于组 1 执行
sig.connect(GoodMorning());                     // 无组，默认排到最后
```

执行顺序：**无组 + `at_front` → 有组（按组号）→ 无组 + `at_back`（默认）**。

### 7.3 连接管理：connection / scoped / block

```cpp
boost::signals2::connection c = sig.connect(HelloWorld());
std::cout << c.connected();   // 1
c.disconnect();               // 显式断开

{   // 作用域连接：出作用域自动断开
    boost::signals2::scoped_connection sc(sig.connect(ShortLived()));
    sig();                    // 会调用 ShortLived
}                              // 出作用域，自动断开
sig();                        // 不再调用

{   // 临时屏蔽（防递归的经典手段）：不解除连接，只是本轮跳过
    boost::signals2::shared_connection_block block(c);
    sig();                    // c 的槽被跳过
}                              // 解除屏蔽
sig();
```

还可以按「等价的函数对象」断开（要求该类型有 `==`）：

```cpp
sig.connect(&foo);
sig.disconnect(&foo);   // 只断 foo，不断 bar
```

### 7.4 自动断开：track（本库的杀手锏）

槽绑定了裸对象指针、对象析构后又 emit → 段错误。`track` 让连接跟随 `shared_ptr` 的生死：

```cpp
boost::signals2::signal<void(const NewsItem&)> deliverNews;

auto area = std::make_shared<NewsMessageArea>();
deliverNews.connect(
    boost::signals2::signal<void(const NewsItem&)>::slot_type(
        &NewsMessageArea::displayNews, area.get(), _1)
        .track(area));          // ← 关键：跟踪 area 的生命周期
// area 过期 → 连接自动断开，emit 时安全跳过
// 而且 Boost 保证：槽正在执行时，被 track 的对象不会中途过期（临时 shared_ptr 续命）
```

- `track` 认 `boost::shared_ptr`；`std::shared_ptr` 用 `track_foreign`。
- 传绑定对象时用 `area.get()`，**不要**直接传 `area`——否则槽自己持有一份 `shared_ptr`，对象永远不过期，track 失效。

### 7.5 返回值：combiner

多个槽的返回值如何合成一个？默认 combiner 返回 `boost::optional<最后一个槽的返回值>`。自定义 combiner 可以做任何聚合，比如「取最大值」：

```cpp
template<typename T>
struct maximum {
    typedef T result_type;
    template<typename InputIterator>
    T operator()(InputIterator first, InputIterator last) const {
        if (first == last) return T();
        T max_value = *first++;
        while (first != last) {
            if (max_value < *first) max_value = *first;
            ++first;
        }
        return max_value;
    }
};

boost::signals2::signal<float(float, float), maximum<float>> sig;
sig.connect(&product);      // 15
sig.connect(&difference);   // 2
std::cout << sig(5, 3);     // 15 —— 聚合所有槽的返回值
```

> 💡 combiner 的迭代器是**惰性求值**的：解引用才调用槽。所以「找到第一个满足条件的就提前 return」也是合法 combiner——事件处理里的"消费即拦截"就是这么实现的（模式 ① 里 `Event::handled` 干的事）。

### 7.6 线程安全：Mutex 模板参数

默认每步操作都加锁（`boost::signals2::mutex`），跨线程 connect/disconnect/emit 安全。单线程程序可以换 `dummy_mutex` 省锁开销：

```cpp
namespace bs2 = boost::signals2;
using namespace bs2::keywords;
bs2::signal_type<void(int), mutex_type<bs2::dummy_mutex>>::type sig;  // 单线程高速版
```

### 7.7 什么时候连接会断开（官方语义）

1. `connection::disconnect()`（含 `scoped_connection` 析构）；
2. 被 `track` 的对象析构；
3. **Signal 自己析构**——所有连接随之消失。

并且：**emit 途中断开的槽，本轮后续不再调用但调用序列继续**；正在执行的槽不会被"打断等它跑完"以外的任何保证（多线程下 disconnect 不等待槽执行完毕）。

---

## 8. 五个主流库横评

| | Qt Signals/Slots | Boost.Signals2 | nano-signal-slot | palacaze::sigslot | EnTT `sigh`/`sink` |
|---|---|---|---|---|---|
| 标准 | C++17 + **MOC 预处理器** | 纯 ISO C++ | 纯 C++17，4 个头文件 | 纯 C++14，单头 | 纯 C++17（ECS 库的一部分） |
| 线程安全 | 队列连接跨线程 | ✅ 默认加锁 | 可选策略（ST/TS/TS_Safe…） | ✅ | 视配置 |
| 自动断开 | QObject 父子机制 | `track` / `track_foreign` | 继承 `Nano::Observer<>` | lifetime tracking（可 ADL 扩展） | 连接存 weak ref |
| 返回值 | 不支持 | ✅ combiner 任意聚合 | `fire_accumulate` 收集 | ❌ 刻意不支持 | ❌ |
| 依赖 | 整个 Qt | Boost（header-only） | 无 | 无 | 引入 EnTT |
| 性能 | 中（字符串查找） | 中（锁 + 分配） | **极高**（基准测试常客） | 高 | 高，为游戏引擎而生 |
| 特色 | 跨线程队列、Designer 集成 | 功能最全、文档最全 | 零分配、`connect<&Foo::bar>(obj)` 编译期绑定 | API 干净、RAII 连接 | **发布权（sigh）与订阅权（sink）分离** |

选型直觉：

```
已在用 Qt                → Qt Signals/Slots（别重复造轮子）
要"全都要"且不怕 Boost   → Boost.Signals2
游戏引擎、追求性能/零依赖 → nano-signal-slot 或 EnTT
只想替换 std::function 多播，越简单越好 → palacaze::sigslot 或本篇 MiniSignal
ECS 架构                 → EnTT（sink/sigh/dispector 一套全有）
```

> 💡 EnTT 的 `sigh`/`sink` 分离值得单独一提：信号本体 `sigh` 是私有的（只有 owner 能 `publish`），对外暴露 `sink`（只能 `connect`/`disconnect`）。**发布者不暴露订阅能力，订阅者不暴露发布能力**——接口权限的干净切分，模式 ① 的 `setEventCallback` 做不到。

---

## 9. 用在你的引擎里

### 9.1 场景 A：把按键回调升级成多播

```cpp
// GLFWManager.h —— 关键改动
#include "mini_signal.hpp"

class GLFWManager {
public:
    // 外部可自由订阅/退订，互不覆盖
    mini::Signal<int, int, int, int> keySignal;   // key, scancode, action, mods
    ...
private:
    static void keyCallback(GLFWwindow* w, int key, int scancode, int action, int mods) {
        auto* self = static_cast<GLFWManager*>(glfwGetWindowUserPointer(w));
        if (self) self->keySignal.emit(key, scancode, action, mods);
    }
};
```

业务侧（ImGui 和游戏输入和平共处）：

```cpp
mini::ScopedConnection uiConn   = window.keySignal.connectScoped(&ImGuiLayer::onKey, &imgui);
mini::ScopedConnection gameConn = window.keySignal.connectScoped([&](int key, int, int action, int) {
    if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE) window.setShouldClose(true);
});
```

### 9.2 场景 B：模块间点对点通知（模式 ② 的主场）

```cpp
class TextureCache {
public:
    mini::Signal<TextureHandle> onTextureLoaded;   // 加载完成信号
    TextureHandle load(const std::string& path) {
        auto h = decodeAndUpload(path);
        onTextureLoaded.emit(h);                   // 谁关心谁订阅
        return h;
    }
};
// 渲染系统：贴图好了刷新材质；音频系统：贴图好了解除占位图——互不知晓对方存在
```

这类「新增一个通知点」不需要像模式 ① 那样新建 Event 子类 + 枚举值 + Dispatcher 分支，一个 Signal 成员就完事。

### 9.3 和模式 ① 的分工边界

| 用模式 ①（Event + Dispatcher） | 用模式 ②（Signal/Slot） |
|---|---|
| 键鼠/窗口事件：类型多、要统一过滤、要 `handled` 拦截链 | 模块间点对点通知：`onTextureLoaded`、`onLevelUnloaded` |
| 事件需要排队/延迟（配合事件队列，模式 ④） | 订阅者需要精确退订/一次性订阅 |
| 全引擎只有一条管道，便于全局监听调试 | 一对多广播且不想要中央总线 |

---

## 10. 常见陷阱

1. **悬垂订阅者**：槽按引用捕获 `[&]` 局部对象、或捕获裸 `this` 的对象先死。→ `ScopedConnection` 成员，或 Boost `track`。
2. **emit 中析构 Signal**：槽里把拥有 Signal 的对象删了。→ 明确"emit 期间不得析构信号源"的项目约定（Boost 语义：正在派发时析构，本轮槽序列仍会跑完）。
3. **递归风暴**：槽 A 触发信号 B，B 的槽又触发 A。→ 加深度上限或用 Boost 的 `shared_connection_block` 屏蔽自身。
4. **以为 emit 期间新订阅的槽会立刻收到**：通用语义是下一轮才生效（MiniSignal §5.1、Boost 同）。
5. **跨线程裸用 MiniSignal**：它没锁。要么单线程使用，要么上 Boost.Signals2 / sigslot 的线程安全版。

---

## 11. 速查表

| 你想做的事 | MiniSignal | Boost.Signals2 |
|---|---|---|
| 声明信号 | `mini::Signal<int, float> sig;` | `boost::signals2::signal<void(int, float)> sig;` |
| 订阅 | `auto id = sig.connect(fn);` | `auto c = sig.connect(fn);` |
| 退订 | `sig.disconnect(id);` | `c.disconnect();` 或 `sig.disconnect(&fn);` |
| RAII 退订 | `auto g = sig.connectScoped(fn);` | `boost::signals2::scoped_connection sc(sig.connect(fn));` |
| 临时屏蔽 | ❌（练习 6 带你加） | `shared_connection_block block(c);` |
| 对象死→自动断开 | ❌ | `slot_type(...).track(sp)` |
| 发布 | `sig.emit(args...);` | `sig(args...);` |
| 收集返回值 | ❌（练习 3 带你写） | 自定义 combiner |
| 保证顺序 | 连接顺序 | 分组 `connect(0, fn)` |
| 线程安全 | ❌ | ✅（默认；单线程可 `dummy_mutex`） |

---

## 🔧 练习

1. **温度报警器**：用 `mini::Signal<float>` 实现 `overheated` 信号，订阅者有三个：屏幕（打印温度）、记录仪（写一条日志）、蜂鸣器（**只响一次**，用槽内自断开）。要求 `emit` 两次后，前两者各收到两次、蜂鸣器只响一次。
2. **输出预测**（不许跑代码，写完再验证）：
   ```cpp
   mini::Signal<int> s;
   mini::Signal<int>::Id b = 0;
   s.connect([&](int) { std::cout << "A"; s.disconnect(b); });
   b = s.connect([](int) { std::cout << "B"; });
   s.emit(1);   // ①
   s.emit(1);   // ②
   ```
   问 ①② 两行各输出什么？
3. **带返回值的信号**：实现 `AccumulateSignal<T, Args...>`，提供 `std::vector<T> emitAccumulate(Args...)`，把所有槽的返回值按调用顺序收集返回。用"按键消费投票"（`bool` 槽 × 3）验证。
4. **一次性订阅**：某 UI 教程层想在窗口收到第一次 `WindowResize` 事件后打印初始尺寸并永久退订。分别用（a）槽内自断开、（b）`connectScoped` + 显式 `disconnect()` 两种写法实现。
5. **选型题**：你的引擎已有模式 ① 的 Event + Dispatcher。以下三处通知，各应该用 模式① / 模式② / 直接函数调用 中的哪个？说明理由：
   - a) 窗口关闭事件（引擎主循环要退出）；
   - b) `TextureCache` 通知"某贴图异步加载完成"；
   - c) `RenderContext` 内部调用 `glfwSwapBuffers`。
6. **升级 MiniSignal**：仿照 Boost 的 `shared_connection_block`，给 `MiniSignal` 加"临时屏蔽"能力：`ScopedBlock block(id)` 构造时屏蔽、析构时恢复，被屏蔽的槽在 emit 时跳过但连接保留。（提示：`Record` 加 `bool blocked`，`emit` 判断 `alive && !blocked`。）

---

## 📝 参考答案

### 1. 温度报警器

```cpp
std::vector<std::string> out;
mini::Signal<float> overheated;              // 参数：当前温度

overheated.connect([&](float t) {            // 订阅者1：屏幕告警
    out.push_back("屏幕: 过热 " + std::to_string(t));
});
overheated.connect([&](float) {              // 订阅者2：记录仪
    out.push_back("记录仪: 写入一条日志");
});
mini::Signal<float>::Id buzzerId = 0;        // 订阅者3：一次性蜂鸣器
buzzerId = overheated.connect([&](float) {
    out.push_back("蜂鸣器: 响一声");
    overheated.disconnect(buzzerId);         // 触发即自断开
});

overheated.emit(101.5f);
overheated.emit(102.0f);
// out 共 5 条：第一轮 3 条 + 第二轮 2 条（蜂鸣器已断开）
```

要点：先声明 `buzzerId` 占位再在 lambda 里引用它——连接句柄要等 `connect` 返回才知道，而槽又必须在 connect 时就传进去，所以捕获的是"将来会被赋值"的那个变量的引用。实测输出：

```text
   屏幕: 过热 101.500000
   记录仪: 写入一条日志
   蜂鸣器: 响一声
   屏幕: 过热 102.000000
   记录仪: 写入一条日志
```

### 2. 输出预测

- ① 输出 `A`。A 在自己被调用时断开 B，而 B 排在 A 后面，本轮还没轮到它就被标记死亡，跳过。
- ② 仍输出 `A`。B 的连接已在上轮被清除，剩下唯一的 A。

核心考点：**disconnect 对"本轮还没调用、排在其后的槽"立即生效**（MiniSignal §5.2 实测场景 4）。跑一下上面练习 1 的程序即可复核。

### 3. 带返回值的信号（AccumulateSignal）

```cpp
template <typename T, typename... Args>
class AccumulateSignal {
public:
    using Slot = std::function<T(Args...)>;
    using Id = std::uint64_t;

    Id connect(Slot slot) {
        m_slots.push_back(std::make_unique<Record>(++m_nextId, std::move(slot)));
        return m_nextId;
    }

    void disconnect(Id id) {
        for (auto& r : m_slots)
            if (r && r->id == id) r->alive = false;
        if (m_emitting == 0) sweep();
    }

    std::vector<T> emitAccumulate(Args... args) {
        std::vector<T> results;
        ++m_emitting;
        const std::size_t n = m_slots.size();   // 与 MiniSignal 相同的快照语义
        for (std::size_t i = 0; i < n; ++i) {
            Record& r = *m_slots[i];
            if (r.alive) results.push_back(r.slot(args...));  // 收集返回值
        }
        --m_emitting;
        if (m_emitting == 0) sweep();
        return results;
    }

private:
    struct Record {
        Id id; Slot slot; bool alive = true;
        Record(Id i, Slot s) : id(i), slot(std::move(s)) {}
    };
    void sweep() {
        m_slots.erase(std::remove_if(m_slots.begin(), m_slots.end(),
                          [](const std::unique_ptr<Record>& r) { return r && !r->alive; }),
                      m_slots.end());
    }
    std::vector<std::unique_ptr<Record>> m_slots;
    Id m_nextId = 0;
    int m_emitting = 0;
};

// 验证：按键消费投票
AccumulateSignal<bool, int> keyQuery;        // “有人想消费这个按键吗？”
keyQuery.connect([](int key) { return key == 256; });   // UI: ESC 归我
keyQuery.connect([](int key) { return key == 32; });    // 输入法: 空格归我
keyQuery.connect([](int) { return false; });            // 其他: 不归我

auto votes = keyQuery.emitAccumulate(256);
// votes == { true, false, false } —— 谁消费了一目了然
```

对比 Boost：这相当于把 combiner 写死成 `aggregate_values<std::vector<T>>`（官方教程同款）。Boost 的 combiner 更强——惰性迭代可以"第一个 true 就停"，本实现则总是调用所有槽。

### 4. 一次性订阅

（a）槽内自断开——和练习 1 的蜂鸣器同构，先占位 `Id` 再自断：

```cpp
mini::Signal<int, int> resize;               // (w, h)
mini::Signal<int, int>::Id myId = 0;
myId = resize.connect([&](int w, int h) {
    std::cout << "initial size " << w << 'x' << h << '\n';
    resize.disconnect(myId);
});
resize.emit(1280, 720);   // 打印
resize.emit(800, 600);    // 无输出
```

（b）`connectScoped` + 显式断开——不需要占位变量，守卫自己就是句柄：

```cpp
auto guard = resize.connectScoped([](int w, int h) {
    std::cout << "initial size " << w << 'x' << h << '\n';
});
resize.emit(1280, 720);
guard.disconnect();       // 显式退订（或让 guard 随对象析构）
resize.emit(800, 600);    // 无输出
```

选 (b) 的理由：`ScopedConnection` 常驻成员时，订阅者的生命周期天然覆盖退订时机（析构自动断开）；选 (a) 的理由：退订条件在"槽内部"才知道（如第一次失败就放弃）。两者语义都实测等价。

### 5. 选型题

- **a) 窗口关闭 → 模式 ①**。它是"窗口/输入事件"大家庭的一员，走统一 Event 管道才能享受 `handled` 拦截、事件过滤链、未来的延迟队列（模式 ④）等基础设施；主循环作为普通订阅者接入。
- **b) 贴图加载完成 → 模式 ②**。点对点、一对多、无需 `handled` 语义、不想为它新建 Event 子类 + 枚举——Signal/Slot 的标准主场（见 §9.2）。
- **c) `glfwSwapBuffers` → 直接调用**。进程内固定的 1 对 1 调用关系，没有任何多播/退订/解耦需求，套任何事件机制都是纯开销。**不是所有通信都值得事件化**。

### 6. 临时屏蔽（ScopedBlock）

在 `Record` 加一个字段，emit 时多判一个条件，其余机制原样复用：

```cpp
// mini_signal.hpp 内 Signal 的改动
struct Record {
    Id id;
    Slot slot;
    bool alive = true;
    bool blocked = false;                     // ← 新增
    Record(Id i, Slot s) : id(i), slot(std::move(s)) {}
};

void emit(Args... args) {
    ++m_emitting;
    const std::size_t n = m_slots.size();
    for (std::size_t i = 0; i < n; ++i) {
        Record& r = *m_slots[i];
        if (r.alive && !r.blocked) r.slot(args...);   // ← 判 blocked
    }
    --m_emitting;
    if (m_emitting == 0) sweep();
}

void setBlocked(Id id, bool b) {              // ← 新增
    for (auto& r : m_slots)
        if (r && r->id == id) r->blocked = b;
}
```

```cpp
// RAII 屏蔽守卫（可与 ScopedConnection 同住一个头文件）
class ScopedBlock {
public:
    template <typename... Args>
    ScopedBlock(Signal<Args...>& sig, typename Signal<Args...>::Id id)
        : m_restore([&sig, id] { sig.setBlocked(id, false); }) {
        sig.setBlocked(id, true);
    }
    ~ScopedBlock() { if (m_restore) m_restore(); }
    ScopedBlock(const ScopedBlock&) = delete;
    ScopedBlock& operator=(const ScopedBlock&) = delete;
private:
    std::function<void()> m_restore;
};
```

用途与 Boost 的 `shared_connection_block` 一致：**防递归**——槽 A 处理时会再次触发本信号，进入 A 前屏蔽自身连接，处理完恢复，既不解除连接也不丢事件。

---

## 参考资料

- [Boost.Signals2 官方教程](https://www.boost.org/doc/libs/latest/doc/html/signals2/tutorial.html)（本篇 §7 全部内容的原始出处）
- [nano-signal-slot](https://github.com/NoAvailableAlias/nano-signal-slot)（纯 C++17、四头文件、基准测试常客）
- [palacaze/sigslot](https://github.com/palacaze/sigslot)（C++14 单头、线程安全、目标就是替换 Boost.Signals2）
- [EnTT Wiki — Events, Signals](https://github.com/skypjack/entt/wiki/Events,-signals-and-everything-in-between)（`sigh`/`sink` 分离设计）
- [Game Programming Patterns — Observer](https://gameprogrammingpatterns.com/observer.html)（信号槽背后的模式本体）
- [Testing C++ signal-slot libraries](https://julienjorge.medium.com/testing-c-signal-slot-libraries-1994eb120826)（多库性能/功能横评）

---

> 下一篇 Extra 👉 [OS 信号处理 signal 完全指南](OS信号处理signal完全指南.md)
