# Extra · `std::bind`、匿名函数 (lambda)、`std::ref` / `std::cref` 完全指南

> 目标：上一篇 [Extra · `std::function` 完全指南](std-function完全指南.md) 讲了"容器"，
> 这一篇讲填进容器里的三种"造可调用对象 / 改参数传递方式"的手段：
> **匿名函数 (lambda)**、**`std::bind`**、**`std::ref` / `std::cref`**。
> 它们常一起出现——例如你会在 `std::bind` 里塞 `std::ref`，又或者在 `std::function` 里放一个 lambda。
> 本文从零讲清三者的本质、底层原理、性能取舍，以及为什么现代 C++ 几乎不用 `std::bind`。

---

## 0. 三者关系：一张图先理清

```
            ┌────────────────────────────────────────────────────┐
            │            "造一个可调用对象"                          │
            └────────────────────────────────────────────────────┘
                              │
        ┌─────────────────────┼─────────────────────┐
        ▼                     ▼                     ▼
   匿名函数 (lambda)      std::bind              std::ref / cref
   ─────────────────    ─────────────────      ─────────────────
   现场定义一个           把"已有函数 + 部分      把对象包装成"引用
   无名函数对象           参数"预绑定成新函数       对象"，让它能按值
   （最灵活、最现代）       （历史包袱、不推荐）       传递却仍指向原对象
                                                       │
                                                       ▼
                                       让 bind / thread / async /
                                       make_tuple 这类"按值收参"
                                       的工具能传引用
```

一句话总结：

- **lambda** = 现场"画"一个函数出来用，没有名字。
- **`std::bind`** = 拿一个**已有的**函数 + 一部分参数，**预先填好**，得到一个参数更少的新函数对象。
- **`std::ref` / `std::cref`** = 把一个对象包装成"引用代理"，让那些**默认按值复制**的接口能传引用。

它们都是 `<functional>` 家族的工具（lambda 是语言特性，但用途上和这族工具配套）。

---

## 1. 匿名函数 (lambda)：现代 C++ 的默认选项

### 1.1 语法解剖

```cpp
[ 捕获列表 ] ( 参数列表 ) mutable -> 返回类型 { 函数体 }
   ↑              ↑          ↑          ↑            ↑
   │              │          │          │            └─ 要执行的代码
   │              │          │          └─ 尾随返回类型（多数情况可省，编译器推导）
   │              │          └─ 允许修改按值捕获的副本（默认 operator() 是 const）
   │              └─ 和普通函数一样的形参
   └─ 告诉编译器："我要把外面哪些变量带进来"
```

最小例子：

```cpp
auto add = [](int a, int b) { return a + b; };
std::cout << add(3, 4);   // 7
```

这个 `add` 不是函数指针，而是一个**编译器生成的、独一无二的类类型对象**（下文 §1.5 详解）。

### 1.2 捕获列表：lambda 的灵魂

捕获决定了"lambda 内部怎么访问外层变量"。这是 lambda 比函数指针强、也比 `std::bind` 直观的核心。

| 写法 | 含义 | 内部存储形式 |
|------|------|------------|
| `[]` | 不捕获 | 无成员变量；可退化为函数指针 |
| `[x]` | 按值捕获 `x`（副本） | 一个 `T x;` 成员 |
| `[&x]` | 按引用捕获 `x` | 一个 `T* x;` 成员（指向 `x`） |
| `[=]` | 按值捕获**所有用到的**外层变量 | 多个值成员 |
| `[&]` | 按引用捕获**所有用到的**外层变量 | 多个指针成员 |
| `[=, &x]` | 默认按值，但 `x` 按引用 | 混合 |
| `[&, x]` | 默认按引用，但 `x` 按值 | 混合 |
| `[this]` | 捕获当前对象的 `this` 指针（成员函数内） | `C* this;` |
| `[*this]` | **按值拷贝**当前对象一份（C++17） | `C this;`（副本） |
| `[x = expr]` | 初始化捕获 / 广义捕获（C++14） | 一个成员，用 `expr` 初始化 |
| `[&x = expr]` | 引用捕获表达式结果（C++14） | 指向 `expr` 结果的指针 |

实战片段：

```cpp
int factor = 10;
std::string name = "Tur";

// 按值拷贝 factor（之后改 factor 不影响 lambda）
auto f1 = [factor](int x) { return x * factor; };

// 按引用捕获，能看到 factor 的实时值
auto f2 = [&factor](int x) { return x * factor; };

// 初始化捕获（C++14）：把一个 std::string 移动进 lambda，避免拷贝
auto f3 = [name = std::move(name)] { std::cout << name; };
// 这一刻 name 已被 move 走，lambda 内持有新副本
```

> ⚠️ **`[=]` 和 `[&]` 不捕获"所有变量"，只捕获"实际用到的"**。没用到的不会进成员，零成本。

### 1.3 一个常被忽略的点：捕获时机 = 定义时

```cpp
int x = 1;
auto byVal  = [x]  { return x; };   // 此刻拷贝一份 x = 1
auto byRef  = [&x] { return x; };   // 此刻记下 &x
x = 999;
std::cout << byVal();   // 1   ← 副本，与后续修改无关
std::cout << byRef();   // 999 ← 仍指向同一个 x
```

**按值捕获 = 定义那一刻的快照；按引用捕获 = 一个指针，永远看当前值。** 这条规则解释了 90% 的 lambda 生命周期 bug。

### 1.4 `mutable`：为什么按值捕获改不了

默认 lambda 的 `operator()` 是 `const` 的，所以按值捕获的副本**读得到、改不动**：

```cpp
int counter = 0;
auto bad = [counter] { return ++counter; };   // ❌ 编译错误：counter 是 const
auto ok  = [counter] mutable { return ++counter; };  // ✅
```

注意：加了 `mutable` 后，`++counter` 改的是 **lambda 自己的副本**，外层的 `counter` 不变。而且**每次调用 `ok`，副本会累加**（每个 lambda 对象有独立的状态）：

```cpp
std::cout << ok() << '\n';  // 1
std::cout << ok() << '\n';  // 2  ← 副本累加
std::cout << counter;       // 0  ← 外层没变
```

### 1.5 底层原理：lambda 就是一个类

写：

```cpp
int factor = 10;
auto f = [factor](int x) { return x * factor; };
```

编译器在背后**生成一个类**（名字你拼写不出来，例如 `__lambda_42`）：

```cpp
struct __lambda_42 {
    int factor;                       // 按值捕获 → 成员变量

    __lambda_42(int f) : factor(f) {}

    int operator()(int x) const {     // 默认 const；mutable 时去掉 const
        return x * factor;
    }
};

// auto f = [factor](int x){...};  等价于：
__lambda_42 f(factor);
```

`[&factor]` 的版本差别仅在成员变成 `int* factor;`（指向外层）。

**这就解释了几件事：**
- 每个 lambda 都有**独一无二的类型**，所以 `auto` 是它的天然归宿；放进容器得靠 `std::function` 做类型擦除（见上篇指南）。
- **按值捕获 = 多了一个数据成员**，sizeof 变大，可能撑破 `std::function` 的 SBO 缓冲。
- **没有捕获的 lambda** 可以**隐式转换为函数指针** `Ret(*)(Args...)`，所以能塞进 C API（GLFW 的回调就是用这条性质）。

```cpp
// GLFW 的 C 回调签名：
// void glfwSetKeyCallback(GLFWwindow*, void(*)(GLFWwindow*, int, int, int, int));

window.setKeyCallback([](GLFWwindow* w, int key, int, int, int action) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(w, GLFW_TRUE);
});
// ↑ 无捕获 lambda → 隐式转成函数指针，C API 能直接收
```

### 1.6 泛型 lambda（C++14）与模板 lambda（C++20）

```cpp
// C++14：参数可以用 auto，等价于一个 operator()(auto x, auto y) 模板
auto less = [](auto a, auto b) { return a < b; };
less(1, 2);          // OK
less("a", "b");      // 也 OK

// C++20：显式模板参数
auto fn = []<typename T>(std::vector<T> const& v) { return v.size(); };
```

新代码直接用泛型 lambda，不用再写一堆仿函数（functor）。

### 1.7 lambda 的演化时间线

| 标准 | 新增能力 |
|------|---------|
| C++11 | lambda 本体、捕获列表、尾随返回类型 |
| C++14 | **泛型 lambda**（`auto` 参数）、**初始化捕获**（`[x = expr]`） |
| C++17 | `constexpr` lambda、`*this` 按值捕获当前对象 |
| C++20 | **模板 lambda**（`[]<typename T>(T)`）、lambda 在未求值上下文可用 |
| C++23 | `static` lambda、`this` 捕获的衰减警告收紧 |

每次升级都在让 lambda 能替代更多 `std::bind` 的活儿——这就是 §3.6 的伏笔。

---

## 2. `std::bind`：函数的"参数预填机"

### 2.1 它解决什么问题

有时你有一个**已经存在**的函数（库里的、别人的、成员函数），但你想**预先填好一部分参数**，得到一个参数更少的新函数对象。这就叫**部分应用**（partial application）。

```cpp
#include <functional>
using namespace std::placeholders;   // 引入 _1, _2, ...

int add(int a, int b, int c) { return a + b + c; }

// 预填前两个参数，留下一个"位置占位符" _1
auto add5and3 = std::bind(add, 5, 3, _1);

std::cout << add5and3(10);   // 18  (= 5 + 3 + 10)
```

`_1` 表示"调用时传进来的第 1 个参数"。多个占位符 `_1, _2, _3, ...` 表示参数位置，**下标从 1 开始**。

### 2.2 参数可以重排、复用

```cpp
auto f = std::bind(add, _3, _1, _2);
f(10, 20, 30);   // add(30, 10, 20) = 60
//         ↑   ↑   ↑
//        _1  _2  _3 → 位置被重排

auto g = std::bind(add, _1, _1, _1);
g(7);            // add(7, 7, 7) = 21   ← 同一参数复用
```

### 2.3 绑定成员函数（最常见的用法）

成员函数隐含一个 `this` 参数，`std::bind` 要把它和对象绑在一起：

```cpp
struct Calculator {
    int offset;
    Calculator(int o) : offset(o) {}
    int add(int a, int b) const { return a + b + offset; }
};

Calculator calc(100);

// 第 1 个参数是 成员函数指针；第 2 个是 对象（指针或引用都行）
auto fn = std::bind(&Calculator::add, &calc, _1, _2);

std::cout << fn(3, 4);   // 107
```

**等价的 lambda 写法**（更短、更直观、可被内联）：

```cpp
auto fn = [&calc](int a, int b) { return calc.add(a, b); };
```

### 2.4 `std::bind` 的返回值类型——你拼写不出来

```cpp
auto b = std::bind(add, _1, _1, _1);
// b 的真实类型大概是：
//   std::_Bind<add(int (*)(int,int,int), std::_Placeholder<1>, ...)>
// 不同标准库实现完全不同，没有人手写这个类型。
```

所以 `std::bind` 的结果**只能用 `auto` 接，或塞进 `std::function`**：

```cpp
std::function<int(int)> fn = std::bind(add, _1, _1, _1);
```

这也意味着 `std::bind` 永远带来一层间接，**几乎不可能被内联优化**。

### 2.5 `std::bind` 按值复制参数——`std::ref` 的常驻地

`std::bind` 会把传入的实参**按值拷贝**进返回的函数对象里。这通常是好事（自带所有权），但有时你想让它**引用**外面的对象：

```cpp
struct BigState { int counter = 0; void tick() { ++counter; } };
BigState state;

// ❌ 这会把 state 拷一份；state.counter 不会变
auto bad = std::bind(&BigState::tick, state);

// ✅ 用 std::ref：内部存引用，调的是外面的 state
auto good = std::bind(&BigState::tick, std::ref(state));

good();
good();
std::cout << state.counter;   // 2
```

`std::ref` 在这里出场——它就是为这种"按值收参但你想传引用"的场景设计的（详见 §3）。

### 2.6 为什么现代 C++ 几乎不用 `std::bind`

Scott Meyers 在《Effective Modern C++》Item 34 标题就是 **"Prefer lambdas to `std::bind`"**；Herb Sutter 和 C++ Core Guidelines 同样建议避开它。理由：

| 问题 | 说明 |
|------|------|
| **错误信息难读** | `std::bind` 的返回类型由标准库内部嵌套模板拼成，一行报错动辄几十屏。 |
| **无法内联** | 中间隔着一层 `std::_Bind` 包装，编译器很难跨过去优化。lambda 直接是一个类，能被完全内联。 |
| **重载函数无法直接 bind** | `std::bind(f, ...)` 里如果 `f` 有重载，编译器不知道绑哪个，你得手写一个函数指针转型。lambda 没这问题。 |
| **参数传递语义晦涩** | 按值？按引用？需不需要 `std::ref`？读代码的人要记住一堆规则。lambda 的 `[=]` / `[&]` 一眼看清。 |
| **move 语义处理差** | C++11 时代的接口设计，难以高效转发只移动类型（`std::unique_ptr` 等）。lambda 的初始化捕获 `[p = std::move(p)]` 干净利落。 |

**结论：新代码一律用 lambda。** 只有两类场景还可能看到 `std::bind`：

1. 维护 C++11 老代码（当时 lambda 还没泛型、没初始化捕获）。
2. 需要做**参数位置重排 / 复用**（`std::bind(f, _2, _1, _1)`）这种 lambda 表达不直观的场景——但这种需求本身也很罕见。

### 2.7 现代替代：`std::bind_front`（C++20）/ `std::bind_back`（C++23）

如果你只是想**绑前 N 个参数**（不做位置重排），C++20 给了一个干净得多的工具：

```cpp
#include <functional>

int add(int a, int b, int c) { return a + b + c; }

// 旧：std::bind(add, 5, 3, std::placeholders::_1)
// 新：
auto add5and3 = std::bind_front(add, 5, 3);
add5and3(10);   // 18

// 绑成员函数也支持，第一个参数是成员指针，第二个是对象
struct Calc { int add(int a, int b) const { return a + b; } };
Calc c;
auto fn = std::bind_front(&Calc::add, &c);
fn(3, 4);   // 7
```

`std::bind_front` 的优势：

- **完美转发**参数和对象（支持 move-only 类型）。
- **返回类型简单**，更容易被编译器优化。
- 不需要 `placeholders`，不会搞混 `_1` 的下标。
- 重载函数可以通过 `static_cast` 或传函数指针干净处理。

> 💡 把 `std::bind_front` 记成"**lambda 的语法糖，专用于部分应用**"。其余需求继续用 lambda。

---

## 3. `std::ref` / `std::cref`：把"引用"塞进按值收参的接口

### 3.1 问题背景

很多标准库设施**默认按值复制**参数：

- `std::bind` / `std::bind_front` 复制传入的可调用对象和实参；
- `std::thread` / `std::async` 按值复制线程函数的参数（**即使你写 `void f(int&)`，线程里也是副本**）；
- `std::make_tuple` / `std::make_pair` 按值复制每个元素；
- 容器 `std::vector<T>` 里 `T` 不能是引用（`vector<int&>` 编译不过）。

这些接口的设计动机是**值语义的安全性**（没有悬垂引用风险）。但当你**确实需要传引用**时怎么办？

`std::ref` / `std::cref` 就是答案：它返回一个**"引用代理"对象**，这个对象本身是可拷贝的值类型，但**行为像引用**。

### 3.2 `std::ref` 到底返回了什么

```cpp
int x = 42;
auto r = std::ref(x);    // r 的类型是 std::reference_wrapper<int>
auto c = std::cref(x);   // c 的类型是 std::reference_wrapper<const int>
```

`std::ref(x)` ≡ `std::reference_wrapper<int>(x)`；
`std::cref(x)` ≡ `std::reference_wrapper<const int>(x)`。

`std::reference_wrapper<T>` 的本质（简化版）：

```cpp
template<typename T>
class reference_wrapper {
    T* ptr;                                  // 内部就是一个指针
public:
    reference_wrapper(T& obj) : ptr(&obj) {} // 从左值构造

    operator T&() const noexcept { return *ptr; }   // 隐式转回 T&
    T& get() const noexcept { return *ptr; }         // 显式取

    template<typename... Args>
    auto operator()(Args&&... args) const            // 如果 T 可调用，它也可调用
        -> decltype(std::invoke(*ptr, std::forward<Args>(args)...));
};
```

关键性质：

| 性质 | 含义 |
|------|------|
| **可拷贝、可赋值**（C++17 起还是 TriviallyCopyable） | 能进容器、能按值传 |
| **隐式转换为 `T&`** | 凡是收 `T&` 的函数都能直接吃它 |
| **可调用**（若 `T` 可调用） | 能放进 `std::function`、能当函数对象 |
| **不能包临时对象** | `std::ref(临时)` 的重载被 `= delete`，编译期拒绝——避免悬垂 |

### 3.3 三个典型用法

#### ① 让 `std::thread` / `std::async` 传引用

```cpp
void increment(int& n) { ++n; }

int counter = 0;

// ❌ 即便函数签名是 int&，thread 仍按值拷贝 counter；编译失败（无法把 int 绑到 int&）
// std::thread t(increment, counter);

// ✅ 用 std::ref 把 counter 包成引用代理
std::thread t(increment, std::ref(counter));
t.join();
std::cout << counter;   // 1
```

这是 `std::ref` **最经典**的应用场景，几乎所有介绍它的教程都从这里开始。

#### ② 让 `std::bind` / 算法按引用工作

```cpp
#include <algorithm>
std::vector<int> v = {1, 2, 3, 4};
int sum = 0;

// std::for_each 按值传 lambda，但 lambda 捕获 [&sum] 也能改 sum
// 这里换一种写法展示 std::ref：用一个"累加器"仿函数
struct Accumulator {
    int& target;
    void operator()(int x) { target += x; }
};

// 如果想用 std::bind 把 sum 绑进去：
auto f = std::bind(Accumulator{std::ref(sum)}, _1);
//                ↑ ↑
//    仿函数临时量   把 sum 按引用传进构造函数
// （这里仅演示 std::ref 的位置；实战请直接 lambda）
std::for_each(v.begin(), v.end(), [&sum](int x){ sum += x; });
```

#### ③ 容器里存"引用"

`std::vector<int&>` 编译不过。但你可以存 `std::reference_wrapper<int>`：

```cpp
int a = 1, b = 2, c = 3;
std::vector<std::reference_wrapper<int>> refs = {std::ref(a), std::ref(b), std::ref(c)};

for (int& x : refs)   // 隐式转 int&，原值可改
    x *= 10;

std::cout << a << b << c;   // 10 20 30
```

这是 `std::reference_wrapper` 设计的**最初动机**之一：让引用类型能装进容器。

### 3.4 `std::ref` vs 引用捕获 lambda

两者都能"传引用"，但属于**不同的抽象层**：

| | `std::ref(x)` | `[&x]{...}` |
|---|---|---|
| **本质** | 一个对象，类型 `reference_wrapper<T>` | 一个编译器生成的类的成员（指针） |
| **能被 `std::thread` / `bind` 识别** | ✅ 这些工具专门识别它 | ❌ 它们只看到 lambda 整体 |
| **能放进容器** | ✅ | ❌（除非再包一层 `std::function`） |
| **语法位置** | 出现在**实参** | 出现在 lambda 的**捕获列表** |
| **典型场景** | 跨"按值收参"的 API 传引用 | 在 lambda 内部访问外层变量 |

**经验法则**：你在写 lambda → 用 `[&x]`；你在调用一个**默认按值的库 API**（`thread`、`bind`、`async`、`make_tuple`）→ 用 `std::ref(x)`。

### 3.5 你项目里的对应场景

翻 [基础篇 04 · 实现基础 GLFWManager](https://github.com/Tur/basics/04-%E4%BD%BF%E7%94%A8Manager%E5%86%99%E5%87%BA%E7%AC%AC%E4%B8%80%E4%B8%AA%E7%AA%97%E5%8F%A3.md) 和 [Extra · `std::function` 完全指南](std-function完全指南.md) §3.3，绑定成员函数用的是 lambda：

```cpp
// 推荐写法（你项目里的风格）
std::function<int(int,int)> fn = [&calc](int a, int b) { return calc.add(a, b); };
```

等价的 `std::bind + std::ref` 写法：

```cpp
std::function<int(int,int)> fn =
    std::bind(&Calculator::add, std::ref(calc), _1, _2);
//                            ↑ 用 ref 确保 calc 按引用绑，而不是被拷贝
```

两行功能一样，第一行**更短、可读、能内联**——这也是 §2.6 结论的具体体现。

---

## 4. 三者结合的实战速写

### 4.1 `std::bind` 内部用 `std::ref` 改外层状态

```cpp
#include <functional>
#include <iostream>

int main() {
    int n = 0;
    auto inc = std::bind([](int& x){ ++x; }, std::ref(n));
    inc(); inc(); inc();
    std::cout << n;   // 3
}
```

`std::ref(n)` 在 bind 对象里存的是引用代理，每次调用转回 `int&`，所以 `n` 真的累加。**纯 lambda 写法**：

```cpp
auto inc = [&n]{ ++n; };
inc(); inc(); inc();
```

——同效果，少一层间接。新代码请用后者。

### 4.2 线程池里把只移动对象送进任务

`std::thread` 按值收参，传 move-only 类型（如 `std::unique_ptr`）要显式 `std::move`：

```cpp
auto p = std::make_unique<int>(42);
std::thread t([](std::unique_ptr<int> up) {
    std::cout << *up;
}, std::move(p));         // lambda 形参按值收，外部 move 进去
t.join();
```

注意这里**不需要** `std::ref`——`std::move` 把 `p` 转成右值，thread 内部完美转发。`std::ref` 适用于**左值引用**场景，不适用于 move 语义。

### 4.3 容器里保存"对同一对象的回调集合"

```cpp
struct Button { std::function<void()> onClick; };

Button b1, b2, b3;
int sharedCounter = 0;

// 三个按钮都改同一个 counter
b1.onClick = [&sharedCounter]{ ++sharedCounter; };
b2.onClick = [&sharedCounter]{ sharedCounter += 10; };
b3.onClick = [&sharedCounter]{ sharedCounter *= 2; };

// 注意：lambda 持有 &sharedCounter，sharedCounter 析构后回调变悬垂（见 §5.1）
```

---

## 5. 常见陷阱

### 5.1 悬垂引用：引用捕获 / `std::ref` 的头号杀手

```cpp
std::function<void()> makeBad() {
    int local = 42;
    return [&local]{ std::cout << local; };   // ❌ local 返回时销毁
}
makeBad()();   // UB：读到栈上已回收的内存
```

`std::ref` 同理：

```cpp
std::function<void()> makeBad2() {
    int local = 42;
    return std::bind([](int& x){ std::cout << x; }, std::ref(local));  // ❌
}
```

✅ **修正**：用按值捕获 `[local]`，或把对象生命周期延长到回调之后（如存为成员、用 `std::shared_ptr` 共享所有权）。

### 5.2 捕获 `this` 后对象被销毁

```cpp
class Widget {
    std::function<void()> m_cb;
public:
    void setup() {
        m_cb = [this]{ std::cout << m_data; };   // [this] = 指针
    }
    int m_data;
};

Widget* w = new Widget;
w->setup();
delete w;
// 此时 m_cb 被外部调用 → UB
```

C++17 起可以 `[this]` 改 `[*this]`，**按值拷贝对象**（要求对象可拷贝）。更安全的现代做法是 `std::shared_ptr` + `[self = shared_from_this()]`。

### 5.3 `std::bind` + 重载函数：编译失败

```cpp
void f(int);
void f(double);

auto b = std::bind(f, _1);   // ❌ 不知道绑哪个 f
```

要靠转型或 lambda 绕过：

```cpp
auto b = std::bind(static_cast<void(*)(int)>(f), _1);  // 啰嗦
auto l = [](int x){ f(x); };                            // 推荐
```

### 5.4 `std::ref(临时对象)` 直接编译失败

```cpp
auto r = std::ref(42);   // ❌ 对 const int&& 的重载被 = delete
```

设计如此：引用一个临时量必然悬垂，编译期就拦下。**如果你真有这个需求，几乎一定是写错了。**

### 5.5 `[=]` 看似安全，仍可能悬垂（捕获 `this` 时）

```cpp
class C {
    void run() {
        // C++20 之前 [=] 会隐式捕获 this（warning）
        // C++20 起 [=] 不再隐式捕获 this，必须显式 [this] 或 [=, this]
        auto cb = [=]{ std::cout << m_x; };
    }
    int m_x;
};
```

新标准收紧了这条规则（GCC/Clang 会发 `-Wdeprecated`），是好事——显式写 `[this]` 让你意识到生命周期问题。

### 5.6 lambda 返回类型推导失败时

```cpp
auto f = [](int x) {
    if (x > 0) return 1;     // int
    return 2.0;              // double ← 推导冲突，编译错误
};
```

✅ 用尾随返回类型：`[](int x) -> double { ... }`。

---

## 6. 决策树：什么时候用什么

```
你要做什么？
│
├─ 现场写一个回调 / 算法谓词 / 简单逻辑
│   └─ ✅ 用 lambda（默认选项，永远首选）
│
├─ 拿一个已有函数，预先填好前几个参数
│   ├─ C++20+：✅ std::bind_front（干净，完美转发）
│   └─ C++17 或更早：✅ 用 lambda 模拟：[捕获前几个]{ 原函数(捕获, args...); }
│       （避免 std::bind，除非你确实需要参数重排）
│
├─ 参数位置重排 / 同一参数复用（_2, _1, _1）
│   └─ 用 lambda 表达不便时，才考虑 std::bind
│
├─ 调用 std::thread / async / bind / make_tuple，但想传引用
│   └─ ✅ std::ref(x) / std::cref(x)
│
├─ 在 vector / map 等容器里存"引用"
│   └─ ✅ vector<std::reference_wrapper<T>>
│
└─ 想给已有的可调用对象 + 一个外层状态，跨 API 传引用
    └─ 看上下文：lambda 内部用 [&x]；跨 API 边界用 std::ref(x)
```

---

## 7. 速查表

| 你想做的事 | 代码 |
|-----------|------|
| 无捕获 lambda | `[](int x){ return x*2; }` |
| 按值捕获 | `[x]{ ... }` 或 `[=]{ ... }` |
| 按引用捕获 | `[&x]{ ... }` 或 `[&]{ ... }` |
| 移动捕获（C++14） | `[p = std::move(p)]{ ... }` |
| 捕获当前对象 | `[this]{ ... }`（或 `[*this]` C++17 按值拷贝） |
| 让按值捕获可修改 | `[x] mutable { ... }` |
| 泛型 lambda（C++14） | `[](auto a, auto b){ return a+b; }` |
| 模板 lambda（C++20） | `[]<typename T>(std::vector<T> const& v){ ... }` |
| 绑前 N 个参数（C++20） | `std::bind_front(f, a, b)` |
| 绑成员函数（推荐） | `[&obj](int x){ obj.m(x); }` |
| 绑成员函数（bind） | `std::bind(&C::m, &obj, _1)` |
| 参数重排 | `std::bind(f, _2, _1)` |
| 给 thread 传引用 | `std::thread(f, std::ref(x))` |
| 给 thread 传 const 引用 | `std::thread(f, std::cref(x))` |
| 容器存引用 | `std::vector<std::reference_wrapper<int>>` |
| 从 `reference_wrapper` 取原对象 | `r.get()` 或隐式转 `T&` |

---

## 8. 总结

```
┌─────────────────────────────────────────────────────────────────┐
│                   三者一句话真面目                                │
├─────────────────────────────────────────────────────────────────┤
│  ✅ lambda  = 现场画一个无名函数对象，捕获列表是它的灵魂            │
│      底层：编译器生成的类，operator() 是入口                       │
│      选型：永远是默认选项，新代码 90% 场景用它                     │
│                                                                  │
│  ⚠️ std::bind = 把已有函数 + 部分参数预填成新函数对象              │
│      底层：返回类型不可拼写，间接调用，几乎不内联                  │
│      选型：避开。需要部分应用用 std::bind_front (C++20) 或 lambda  │
│                                                                  │
│  ✅ std::ref / cref = 把对象包成"引用代理"                         │
│      底层：reference_wrapper，内部一个指针，TriviallyCopyable      │
│      选型：跨"按值收参"的 API（thread/bind/async/make_tuple）      │
│            传引用；或在容器里存引用                                │
│                                                                  │
│  组合关系：                                                       │
│    std::bind(f, std::ref(x))     ← bind 默认拷贝，ref 让它引用     │
│    std::function<...> = [...]{}  ← lambda 是 function 最常见的填料 │
│    std::thread(f, std::ref(x))   ← ref 的经典主场                  │
└─────────────────────────────────────────────────────────────────┘
```

**在你的项目里：** 翻 [基础篇 04](https://github.com/Tur/basics/04-%E4%BD%BF%E7%94%A8Manager%E5%86%99%E5%87%BA%E7%AC%AC%E4%B8%80%E4%B8%AA%E7%AA%97%E5%8F%A3.md)、[进阶 01](https://github.com/Tur/advanced/01-%E4%BA%8B%E4%BB%B6%E7%B3%BB%E7%BB%9F%E8%AE%BE%E8%AE%A1.md) 和 [进阶 03](https://github.com/Tur/advanced/03-%E8%BE%93%E5%85%A5%E7%B3%BB%E7%BB%9F.md)，所有回调都是 **lambda**——这就是现代 C++ 的标准姿态。`std::bind` 你基本不会写，但**读老代码、看 GLFW/ImGui 这类库的文档示例时**会遇到它，知道它在干什么即可。`std::ref` / `std::cref` 在你引入**多线程**（[进阶 07](https://github.com/Tur/advanced/07-%E6%80%A7%E8%83%BD%E4%B8%8E%E5%A4%9A%E7%BA%BF%E7%A8%8B.md)）和需要给线程传引用的那一刻就会派上用场。

把这三者和上一篇的 [`std::function`](std-function完全指南.md) 串起来，你就掌握了 C++ "可调用对象"生态的全部主流工具：`std::function` 是容器，lambda / bind / 仿函数是填料，`std::ref` 是让填料按引用而非按值工作的胶水。

---

> 上一篇 Extra 👈 [`std::function` 完全指南](std-function完全指南.md)
> 下一篇 Extra 👉 [事件系统架构全景：七种模式对比](事件系统架构全景-七种模式对比.md)

---

### 参考资料

- [cppreference: `std::ref`, `std::cref`](https://en.cppreference.com/cpp/utility/functional/ref)
- [cppreference: `std::reference_wrapper`](https://en.cppreference.com/w/cpp/utility/functional/reference_wrapper)
- [cppreference: `std::bind`](https://en.cppreference.com/w/cpp/utility/functional/bind)
- [cppreference: `std::bind_front` (C++20)](https://en.cppreference.com/w/cpp/utility/functional/bind_front)
- Scott Meyers, *Effective Modern C++*, Item 34: "Prefer lambdas to `std::bind`"
- [C++ Core Guidelines — F.50: Use a lambda when capturing](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#f50-use-a-lambda-when-a-function-wont-do)
