# Extra · `std::function` 完全指南

> 目标：基础篇和进阶篇到处都在用 `std::function`——回调、事件、过滤器——
> 但它是怎么工作的？什么时候该用它，什么时候不该用？
> 本文从零讲清楚 `std::function` 的本质、用法、开销，以及和函数指针/template 的取舍。

---

## 1. 它到底是什么？

**一句话：`std::function` 是一个能装下"任何可调用对象"的类型擦除容器。**

```cpp
#include <functional>

// 声明：能装下"返回值 void、参数 int" 的任何可调用对象
std::function<void(int)> handler;

// 下面这些东西都能装进去：
handler = [](int x) { std::cout << x; };        // lambda
handler = &someFreeFunction;                      // 普通函数
handler = std::bind(&MyClass::method, &obj, _1);  // 成员函数 + 对象绑一起
handler = MyFunctor{};                            // 仿函数 (operator())
```

**关键：** 不同类型的东西（lambda、函数指针、bind 结果）都有不同的类型，但 `std::function<void(int)>` 给了它们一个**统一的类型**。这就是"类型擦除"（type erasure）——它记住了要调什么，但对外只暴露出 `void(int)` 签名。

---

## 2. 你的项目里已经在用它了

翻开 [基础篇 04](https://github.com/Tur/basics/04-%E5%AE%9E%E7%8E%B0%E5%9F%BA%E7%A1%80GLFWManager.md)，`GLFWManager` 里的回调：

```cpp
using KeyCallback    = std::function<void(int key, int scancode, int action, int mods)>;
using ResizeCallback = std::function<void(int width, int height)>;

KeyCallback    m_keyCb;
ResizeCallback m_resizeCb;

// 用户注册时传入一个 lambda
window.setKeyCallback([](int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE) window.setShouldClose(true);
});
```

再到 [进阶 01](https://github.com/Tur/advanced/01-%E4%BA%8B%E4%BB%B6%E7%B3%BB%E7%BB%9F%E8%AE%BE%E8%AE%A1.md)，整个 Event 通道也靠它：

```cpp
using EventCallback = std::function<void(Event&)>;
EventCallback m_eventCb;

// C 回调里用它
self->m_eventCb(e);

// 业务侧注册
window.setEventCallback([](Event& e) {
    EventDispatcher d(e);
    d.dispatch<KeyPressedEvent>([](KeyPressedEvent& ev) {
        // ...
        return false;
    });
});
```

**`std::function` 在这里扮演的角色：C API（GLFW 函数指针）和 C++ 业务代码（lambda）之间的胶水。**

---

## 3. 四种最常用的构造方式

### 3.1 装 lambda

```cpp
std::function<int(int, int)> op;

// 捕获列表为空的 lambda → 可退化成函数指针，开销最小
op = [](int a, int b) { return a + b; };

// 带捕获的 lambda → 需要分配（小对象可能被 SBO 优化掉）
int factor = 10;
op = [factor](int a, int b) { return (a + b) * factor; };

int result = op(3, 4);  // 70
```

### 3.2 装普通函数

```cpp
int add(int a, int b) { return a + b; }

std::function<int(int, int)> fn = &add;   // & 可省略
fn = add;                                  // 函数名自动退化为指针
```

### 3.3 装成员函数（三步走）

成员函数隐含 `this` 参数，所以必须绑上对象：

```cpp
class Calculator {
    int m_offset;
public:
    Calculator(int o) : m_offset(o) {}
    int add(int a, int b) const { return a + b + m_offset; }
};

Calculator calc(100);

// 方法 A：std::bind（经典但啰嗦）
std::function<int(int, int)> fn1 =
    std::bind(&Calculator::add, &calc, std::placeholders::_1, std::placeholders::_2);

// 方法 B：lambda 捕获（推荐，更直观）
std::function<int(int, int)> fn2 =
    [&calc](int a, int b) { return calc.add(a, b); };

// 都可以
fn1(3, 4); // 107
fn2(3, 4); // 107
```

> 💡 **推荐 lambda 捕获而非 `std::bind`**：lambda 更容易读、可以被内联优化。
> `std::bind` 创造的中间对象类型很复杂，编译错误信息也很难看。
> 仅在需要 `std::placeholders` 做参数位置重排时考虑 bind。

### 3.4 装仿函数（Functor）

```cpp
struct Multiplier {
    int factor;
    int operator()(int x) const { return x * factor; }
};

std::function<int(int)> fn = Multiplier{5};
fn(10);  // 50
```

---

## 4. 类型擦除的底层原理（精简化）

你写 `std::function<void(int)>` 时，编译器在内部干了这些事：

```
┌─────────────────────────────────┐
│  std::function<void(int)>       │
│  ┌───────────────────────────┐  │
│  │  vtable* ───────────────► │──┼──► invoke (统一的调用入口)
│  │  small_buffer[NN bytes]   │  │    clone  (拷贝时用)
│  │                           │  │    destroy(析构时用)
│  └───────────────────────────┘  │
└─────────────────────────────────┘
  ↑ small_buffer 直接存小对象
  ↑ 大对象存堆指针，small_buffer 存那个指针
```

核心三步：

| 步骤 | 发生了什么 |
|------|-----------|
| **构造** | 模板构造函数`function(F&&)`拿到可调用对象的具体类型`T`，在堆上（或 SBO 缓冲区里）new 一个 `T`，同时创建一个虚表指向 `invoke/clone/destroy` |
| **调用** | `operator()` → 通过虚表找 `invoke` → 执行真正的 `operator()` |
| **析构** | 通过虚表找 `destroy` → 调用 `T::~T()` → 释放存储 |

这套机制叫做**类型擦除**：所有具体类型（lambda、函数指针、bind 对象）都被擦成了"知道怎么调、怎么拷、怎么销毁"的统一接口。

### 小对象优化 (SBO)

如果放进来的可调用对象足够小，它不会分配堆内存，直接存在 `std::function` 内部的缓冲区里：

```cpp
// 这个 lambda 没有捕获，sizeof 很小 → 大概率 SBO，不分配
std::function<void(int)> f = [](int x) { std::cout << x; };

// 这个 lambda 捕获了一个 std::string → 太大，会分配堆内存
std::string msg = "long message ...";
std::function<void()> f = [msg]() { std::cout << msg; };
```

> 📏 常见实现的 SBO 缓冲区大小：GCC libstdc++ ~16 字节，Clang libc++ ~24 字节，MSVC ~32 字节。
> 一个捕获几个 `int`/指针的 lambda 通常不会被分配到堆上。

---

## 5. 检查、清空、移动

```cpp
// 默认构造的 std::function 是"空的"
std::function<void(int)> fn;
// 此时 fn == nullptr  为 true
// 此时 if (fn)        为 false

// 赋值为有效调用对象
fn = [](int x) { std::cout << x; };
// 此时 if (fn)        为 true

// 清空
fn = nullptr;

// 移动（高效，只搬指针）
std::function<void(int)> fn2 = std::move(fn);   // fn 变空，fn2 持有

// 拷贝（有成本，会真正复制底下的可调用对象）
std::function<void(int)> fn3 = fn2;   // fn2 和 fn3 各持有一份
```

> ⚠️ `std::function` 拷贝时，如果底层对象很大或不可拷贝，编译失败。
> 因此传 `std::function` 给函数时，能用 `const&` 或 `&&` 就别按值传。

---

## 6. 性能：什么时候不该用 `std::function`

### 开销来源

| 来源 | 说明 |
|------|------|
| **虚函数调用** | `operator()` 内部走虚表，不内联 |
| **堆分配** | 捕获了大对象的 lambda 会 `new`（SBO 缓解了部分） |
| **拷贝代价** | 赋值/传值 = 深拷贝底层对象 |

### 对比三种方案

```cpp
// 方案 A：std::function（灵活但有开销）
std::function<int(int)> fn;

// 方案 B：模板（编译期决定，零开销，但不能存到容器里）
template<typename F>
int execute(F&& fn, int x) { return fn(x); }

// 方案 C：裸函数指针（零开销，但只能装纯函数）
int (*fn)(int);
```

| | `std::function` | 模板 (template) | 函数指针 |
|---|---|---|---|
| 能装 lambda (有捕获) | ✅ | ✅ | ❌ |
| 能装成员函数 | ✅ | ✅ | ❌ |
| 能存到容器里 | ✅ | ❌（异类型不行） | ✅ |
| 运行时切换 | ✅ | ❌ | ✅ |
| 内联 | ❌（虚调用） | ✅（编译期展开） | ✅（但要 LTO） |
| 堆分配 | 可能 | 无 | 无 |

### 使用决策

```
    ┌─ 回调只在编译期确定？（如算法模板）
    │   └─ 用 template<F>，零开销
    │
    ┌─ 需要存到成员变量/容器里？
    │   └─ 用 std::function
    │
    ┌─ 只需装纯函数，不需要捕获？
    │   └─ 用裸函数指针 `void (*)(int)`
    │
    └─ 热路径？逐帧/每像素调用几百万次？
        └─ 用 template 或函数指针；绝对不要用 std::function
```

**在你的引擎里：** 事件回调（每帧几十个事件）→ `std::function` 完全没问题。渲染循环内的 draw call 回调 → 绝对不能用。

---

## 7. `std::ref` / `std::cref`：把引用塞进 `std::function`

`std::function` 按值存储，lambda 捕获也按值存储。但有时候你需要传引用：

```cpp
class ExpensiveObject { /* 很大 */ };

ExpensiveObject obj;

// ❌ 这样会在 lambda 里拷贝 obj
std::function<void()> bad = [obj]() { obj.doSomething(); };

// ✅ 用引用捕获
std::function<void()> good = [&obj]() { obj.doSomething(); };
```

但注意：`[&obj]` 创建的 lambda 只持有**指针**，不持有对象。如果 `obj` 在 lambda 调用前析构了 → 悬垂引用 → UB。

**安全规则：** 引用捕获的 lambda 只适合「lambda 生命周期完全包含在对象生命周期内」的场景——比如你在构造函数里注册回调，对象析构前解注册。

---

## 8. 常见陷阱

### 8.1 生命周期：悬垂引用

```cpp
std::function<void()> createBadCallback() {
    int local = 42;
    return [&local]() { std::cout << local; };  // ❌ local 已经没了
}
// 调用返回的 std::function → UB
```

✅ 修正：`[local]` 按值捕获，或 `[=]()`。

### 8.2 拷贝代价

```cpp
class System {
    std::function<void(Event&)> m_cb;

public:
    // ❌ 按值传，会拷贝
    void setCallback(std::function<void(Event&)> cb) {
        m_cb = cb;   // 又拷贝一次（共两次）
    }

    // ✅ 传右值引用，外部 std::move 进来，只移动内部指针
    void setCallback(std::function<void(Event&)>&& cb) {
        m_cb = std::move(cb);   // 零开销
    }
};
```

你项目里的正确示范：
```cpp
void setEventCallback(EventCallback cb) { m_eventCb = std::move(cb); }
//    按值 ————→ 如果外面也用 std::move，只动两次指针；不 move 则一次拷贝一次移动
```

### 8.3 类型不匹配但编译通过

```cpp
std::function<void(int)> fn = [](double x) { std::cout << x; };
fn(42);  // OK，int → double 隐式转换
fn(3.14); // OK
```

**`std::function` 的参数是"兼容即可"**，不是强制执行精确匹配——
只要实参能隐式转换为目标类型，就合法。这是便利，也是隐患（隐式转换的性能开销被隐藏了）。

### 8.4 忘了判空

```cpp
std::function<void(int)> fn; // 空的
fn(42);  // ⚠️ 抛 std::bad_function_call 异常（或 UB，取决于实现）
```

✅ 养成习惯：
```cpp
if (fn) fn(42);
// 或者用你的项目里的模式
if (self && self->m_eventCb) self->m_eventCb(e);
```

---

## 9. 高级技巧

### 9.1 往 vector 里存不同的 lambda（同一签名）

```cpp
std::vector<std::function<void()>> tasks;

tasks.push_back([]{ std::cout << "Task 1\n"; });
tasks.push_back([&someObj]{ someObj.update(); });
tasks.push_back(std::bind(&MyClass::method, &obj));

for (auto& task : tasks)
    task();
```

这就是 [进阶 01 练习 3](https://github.com/Tur/advanced/01-%E4%BA%8B%E4%BB%B6%E7%B3%BB%E7%BB%9F%E8%AE%BE%E8%AE%A1.md) 的中间件链底层原理：`std::vector<EventFilter>` 里的每个 filter 都是不同 lambda，但类型统一。

### 9.2 用 lambda 包装 C 风格回调

```cpp
// C 库要求的签名：void callback(void* userdata, int result)
// 你希望它调你的成员函数

class Downloader {
    std::function<void(int)> m_onDone;

    static void cCallback(void* userdata, int result) {
        auto* self = static_cast<Downloader*>(userdata);
        if (self->m_onDone) self->m_onDone(result);
    }

public:
    void startDownload(const std::string& url, std::function<void(int)> onDone) {
        m_onDone = std::move(onDone);
        c_library_download(url.c_str(), &Downloader::cCallback, this);
    }
};
```

这就是 `GLFWManager` 里 `keyCallback` 的模式：C 回调 → user pointer → 成员 `std::function`。

### 9.3 递归 lambda 需要 `std::function`

```cpp
// ❌ auto 不知道怎么递归
auto fib_bad = [](int n) -> int {
    // return n <= 1 ? n : fib_bad(n-1) + fib_bad(n-2); // fib_bad 的名字还没绑定
};

// ✅ 用 std::function 显式类型
std::function<int(int)> fib = [&](int n) -> int {
    return n <= 1 ? n : fib(n-1) + fib(n-2);
};
fib(10);  // 55
```

---

## 10. 速查表

| 你想做的事 | 代码 |
|-----------|------|
| 声明 | `std::function<返回类型(参数类型...)>` |
| 赋值 lambda | `fn = [](int x) { return x*2; };` |
| 赋值成员函数 | `fn = [&obj](int x) { return obj.m(x); };` |
| 赋值 bind | `fn = std::bind(&C::m, &obj, _1, _2);` |
| 赋值为空 | `fn = nullptr;` |
| 判断是否可调用 | `if (fn) { fn(42); }` |
| 调用 | `fn(arg1, arg2);` 或 `fn(std::forward<Args>(args)...);` |
| 移动（零开销） | `fn2 = std::move(fn);` |
| 拷贝（有分配） | `fn2 = fn;` |
| 作为参数（只看不存） | `void f(const std::function<void(int)>& cb)` |
| 作为参数（拿走） | `void f(std::function<void(int)>&& cb)` 或按值 + 内部 `std::move` |
| 按签名查 type_info | `fn.target_type().name()` |

---

## 11. 总结

```
┌─────────────────────────────────────────────────────────┐
│                   std::function 的真面目                  │
├─────────────────────────────────────────────────────────┤
│  ✅ 类型擦除：lambda、函数指针、仿函数、bind → 统一类型    │
│  ✅ 灵活性：可以存成员变量、放 vector、运行时切换          │
│  ✅ SBO：小可调用对象不分配堆内存                          │
│  ⚠️ 开销：虚函数调用（不内联）+ 可能堆分配                  │
│  ⚠️ 不要：用在热路径、每帧百万次调用的场景                  │
│  💡 最佳场景：事件回调、观察者列表、中间件链、               │
│             C API ↔ C++ 桥接（你项目里的用法）              │
└─────────────────────────────────────────────────────────┘
```

**在你的项目里：** `std::function` 已经是事件系统的基础设施。理解它的类型擦除本质，你就理解了为什么 `setEventCallback` 能接受任何 lambda，以及为什么进阶 01 能从"每个事件一个 callback"重构成"一个统一通道"——底层的 `EventCallback` 不过是一个 `std::function<void(Event&)>`。

---

> 下一篇 Extra 👉 [事件系统架构全景：七种模式对比](事件系统架构全景-七种模式对比.md)
