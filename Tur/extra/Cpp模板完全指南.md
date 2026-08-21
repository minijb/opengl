# Extra · C++ 模板完全指南

> 目标：你的 [事件系统](01-事件系统设计.md) 里那个 `EventDispatcher::dispatch<T>` 为什么能
> "传类型进去、自动做 `static_cast`"？GLFW 回调为什么用 `std::function` 而事件分发却用模板？
> 本文从零讲清楚 C++ 模板的本质、语法、元编程，以及什么时候该用它、什么时候该用虚函数 / `std::function`。
> 全部 10 道练习均附编译运行验证过的参考答案。

> 📌 前置：看得懂基础 C++（类、引用、`const`、`std::vector`）。代码基于 **C++17**，
> 标 ⭐ 的章节（Concepts）需要 **C++20**。全部答案在本仓库环境（MSVC 19.44 / `/std:c++17`、C++20 处单独标注）编译运行通过。

---

## 1. 模板到底是什么

**一句话：模板是给编译器看的"图纸"，编译器按你提供的类型现场生成代码。**

```cpp
template<typename T>
T add(T a, T b) { return a + b; }

int    i = add(1, 2);        // 编译器生成 int add(int, int)
double d = add(1.5, 2.5);    // 编译器生成 double add(double, double)
```

你写了一份 `add`，编译器替你写了两份。这个过程叫**实例化（instantiation）**——它发生在**编译期**，不是运行期。

[cppreference 原话](https://en.cppreference.com/w/cpp/language/function_template)：
> A function template by itself is not a type, or a function. No code is generated from a source file that contains only template definitions.
> （函数模板本身不是类型也不是函数；只包含模板定义的文件不会生成任何代码。必须实例化后才有真正的函数。）

### 1.1 为什么不用宏 / `void*`

| 方案 | 类型安全 | 错误发生时机 | 调试体验 |
|---|---|---|---|
| 宏 `#define ADD(a,b) ((a)+(b))` | ❌ 无条件展开 | 展开后的乱码报错 | 糟糕 |
| `void*` + 转换 | ❌ 运行期才能发现转错 | 运行期 | 一般 |
| **模板** | ✅ 编译期检查 | 编译期、报错指向模板行 | 好（C++20 后更好） |

`void*` 还有个致命伤：模板能在**编译期**对类型做优化（内联、特化），`void*` 不行。

### 1.2 编译期发生的代价

实例化不是免费的：每个不同的 `T` 都会让编译器多走一遍模板体。`add<int>`、`add<double>`、`add<float>` 是**三个不同的函数**。这带来两件事：

- **代码膨胀**（code bloat）：模板用多了二进制变大（现代编译器会做公共子表达式合并，缓解但不会根除）。
- **编译时间变长**：复杂模板（尤其元编程）是编译时间的头号杀手。

---

## 2. 函数模板

### 2.1 基本语法

```cpp
template<typename T>          // 或者 template<class T>，两者完全等价
const T& mymax(const T& a, const T& b) {
    return a < b ? b : a;
}
```

要点：

- `typename` / `class` 在模板参数里**完全等价**（历史原因：模板先于 `typename` 关键字出现）。
- 参数用 **`const T&`**：传 `std::string`、`glm::vec3` 这种大对象时避免拷贝。传 `int` 时编译器照样内联，没有性能损失。
- 返回值也用 `const T&`：返回的是 a 或 b 的引用，避免拷贝。

### 2.2 模板实参推导（deduction）

```cpp
mymax(3, 5);                  // T 从实参推导为 int
mymax(std::string("a"), std::string("b"));  // T = std::string

// 两个实参类型不同 → 推导冲突 → 编译错误：
// mymax(1, 2.5);   // error: 无法同时推导 T = int 和 T = double
```

推不出来时显式指定：

```cpp
mymax<double>(1, 2.5);        // 显式指定 T = double，1 隐式转成 double
```

### 2.3 与重载的关系

```cpp
template<typename T>
const T& mymax(const T& a, const T& b);   // 模板
const char* mymax(const char* a, const char* b);  // 非模板重载，比 "abc" < "abd" 比较的是指针地址，通常是错的
```

调用时：**完全匹配的非模板优先于模板**（实参是 `const char*` 时走非模板版）。模板推导失败不会报错，编译器继续找下一个重载——这条规则是第 7 节 SFINAE 的地基。

### 2.4 缩写函数模板（C++20 ⭐）

C++20 里 `auto` 参数本身就是模板：

```cpp
void f1(auto x);              // 等价于 template<typename T> void f1(T)
```

结合第 8 节 Concepts 可以写成 `void f2(Summable auto x);`。

---

## 3. 类模板

### 3.1 基本语法

```cpp
template<typename T>
class Stack {
public:
    void push(const T& v) { m_data.push_back(v); }
    void pop()             { m_data.pop_back(); }
    const T& top() const   { return m_data.back(); }
    bool empty() const     { return m_data.empty(); }
    std::size_t size() const { return m_data.size(); }
private:
    std::vector<T> m_data;
};

Stack<int> intStack;
Stack<std::string> strStack;
```

类模板的成员函数**定义在类外**时必须重复模板头：

```cpp
template<typename T>
void Stack<T>::push(const T& v) { m_data.push_back(v); }
```

### 3.2 非类型模板参数（Nontype template parameter）

模板参数不一定是类型，还可以是**编译期常量**：

```cpp
template<typename T, std::size_t N>
class FixedArray {
public:
    constexpr FixedArray() : m_data{} {}   // 清零初始化 → 才能有 constexpr 对象
    T& operator[](std::size_t i)       { return m_data[i]; }
    const T& operator[](std::size_t i) const { return m_data[i]; }
    constexpr std::size_t size() const { return N; }   // N 是编译期常量，size() 可编译期求值
private:
    T m_data[N];
};

FixedArray<float, 4> vec4;    // 栈上 4 个 float，零动态分配
```

`std::array<T, N>` 就是这么实现的——这就是 `std::array` 比 `std::vector` 快的原因之一：大小是编译期常量，能放进栈上，`size()` 不查任何运行时状态。

C++17 起非类型参数可以用 `auto` 推导（`template<auto V>`），C++20 起浮点、类类型也能当非类型参数。

### 3.3 CTAD（类模板实参推导，C++17）

```cpp
std::vector v{1, 2, 3};       // C++17 起：T 从初始化列表推导为 int
```

注意：C++17 只对"能推导"的构造器生效。`Stack<int>{1,2,3}` 不能直接写 `Stack{1,2,3}`，除非你提供**推导指引**（deduction guide）：

```cpp
template<typename... Ts>
Stack(Ts...) -> Stack<std::common_type_t<Ts...>>;
```

### 3.4 成员模板 & 类型成员

类里还可以再开模板，标准库里到处都是：`std::vector<T>::emplace_back(Args&&...)` 就是成员函数模板。

模板的"类型成员"（如 `typename T::value_type`）是元编程的积木，第 6 节会用到。

---

## 4. 特化与偏特化

### 4.1 全特化（explicit specialization）

对**某个具体类型**单独写一份实现：

```cpp
template<typename T>
std::string typeName() { return "unknown"; }

template<>
std::string typeName<int>() { return "int"; }   // 全特化

static_assert(typeName<int>() == "int");
static_assert(typeName<double>() == "unknown");
```

### 4.2 偏特化（partial specialization）

**只对类模板生效**——函数模板不允许偏特化（可以重载替代，见 §2.3）。

```cpp
template<typename T>
class Box { public: static constexpr const char* kind = "value"; };

template<typename T>
class Box<T*> { public: static constexpr const char* kind = "pointer"; };  // 对任意指针偏特化

Box<int>    b1;  // "value"
Box<int*>   b2;  // "pointer"
```

### 4.3 谁在用特化

**type traits 就是靠特化实现的**。看 `std::is_pointer` 的简化原理：

```cpp
template<typename T> struct is_pointer        { static constexpr bool value = false; };
template<typename T> struct is_pointer<T*>    { static constexpr bool value = true;  };

static_assert(!is_pointer<int>::value);
static_assert( is_pointer<int*>::value);
```

这就是"编译期问问题"（metaprogramming）的起点：**用特化让编译器在类型上做分支**。

> ⚠️ **坑**：全特化必须定义在实例化之前（同 TU 内），否则 UB（不要求诊断）。

---

## 5. 变参模板（Variadic templates）

### 5.1 参数包

```cpp
template<typename... Ts>
void printAll(Ts... args) {
    std::cout << sizeof...(Ts) << " arguments\n";   // 包里的类型个数
    // (std::cout << ... << args) << '\n';          // C++17 fold，见下
}
```

- `Ts...` 是**模板参数包**（类型包），`args...` 是**函数参数包**（值包）。
- 包可以为零个或多个：`printAll()` 合法。
- 你项目里的 `EventDispatcher::dispatch<T, F>` 虽然只用了两个类型参数，但标准库 `std::function`、`std::tuple`、`std::make_unique` 全部依赖参数包——`make_unique<T>(args...)` 就是把参数包转发给构造函数。

### 5.2 包展开

```cpp
template<typename... Ts>
void printAll(Ts... args) {
    (std::cout << ... << args) << '\n';   // 一元右折叠：展开成 cout << a << b << c
}
```

C++17 的 **fold 表达式**把"包逐个展开"压缩成一个运算符表达式。四种形态：

| 写法 | 语义 | 空包的值 |
|---|---|---|
| `(args + ...)` | 右折叠 | 无（不允许空包） |
| `(... + args)` | 左折叠 | 无（不允许空包） |
| `(0 + ... + args)` | 带初值右折叠 | `0` |
| `(... + args + 0)` | 带初值左折叠 | `0` |

```cpp
template<typename... Ts>
auto sumAll(Ts... args) {
    return (args + ... + 0);   // 空包也合法，返回 0
}
```

### 5.3 递归展开（C++11 时代的写法）

C++17 之前没有 fold，靠"取一个 + 递归"：

```cpp
void printEach() {}                                  // 递归终点：零个参数
template<typename T, typename... Rest>
void printEach(const T& first, const Rest&... rest) {
    std::cout << first << ' ';
    printEach(rest...);                              // 包缩减一个，最终命中空重载
}
```

理解了这段，"递归 + 特化终点"就是所有经典模板元编程的骨架（第 6、10 节）。

---

## 6. 编译期分支：`if constexpr` 与 `constexpr`

### 6.1 `if constexpr`（C++17）

普通 `if` 的**两个分支都要能编译**；`if constexpr` 只编译条件成立的那个分支，另一分支直接丢弃。

```cpp
template<typename T>
void describe(const T& v) {
    if constexpr (std::is_integral_v<T>) {
        std::cout << "整数: " << v << '\n';
    } else if constexpr (std::is_floating_point_v<T>) {
        std::cout << "浮点: " << v << '\n';
    } else {
        std::cout << "其他\n";
    }
}

describe(42);       // 整数
describe(3.14);     // 浮点
describe("hi");     // 其他
```

没有 `if constexpr` 时这段代码编译不过：`std::string` 传入时 `v` 作为整数打印的那行会产生非法代码。有了它，**类型不同，编译出的代码不同**——这就是"编译期分支"。

### 6.2 `constexpr` 函数：编译期计算

```cpp
constexpr int factorial(int n) {
    return n <= 1 ? 1 : n * factorial(n - 1);
}

static_assert(factorial(5) == 120);   // 编译期就算完，运行期零开销
int runtime = factorial(10);          // 实参非编译期常量 → 退回运行期计算，代码只有一份
```

`constexpr` 函数"能编译期算就编译期算，不能就运行期算"，代码只写一份。这是元编程的现代主力——比第 10 节的"类型级递归"更直观，能覆盖绝大多数需求。

### 6.3 `static_assert`：编译期断言的哨兵

```cpp
static_assert(sizeof(int) == 4, "int 必须是 4 字节");
```

模板场景最常用：

```cpp
template<typename T>
void onlyForIntegers(T v) {
    static_assert(std::is_integral_v<T>, "只接受整数类型");
}
```

> ⚠️ **坑**：`if constexpr` 条件里的表达式也必须是编译期可求值的（type traits、`constexpr` 函数、`sizeof`、`decltype` 都行；普通变量不行）。

---

## 7. SFINAE：替换失败不是错误

### 7.1 原理

模板实例化时，编译器把 `T` 代入模板签名（比如返回类型、参数类型）。如果代入后**某处语法非法**，编译器不会直接报错，而是把**这个候选从重载集中剔除**，继续找别的。这就是 SFINAE（Substitution Failure Is Not An Error）。

经典例子：只对"有 `size()` 成员"的类型启用：

```cpp
template<typename T>
auto getSize(const T& v) -> decltype(v.size()) {   // 返回类型里检测 v.size() 是否合法
    return v.size();
}

// 兜底版本：没有 size() 的（如 int）走这里
std::size_t getSize(...) { return 0; }

getSize(std::string("abc"));   // 3，走模板版
getSize(42);                   // 0，int 没有 size() → 模板被剔除 → 走 ... 版
```

### 7.2 `std::enable_if`：显式开关

```cpp
template<typename T>
std::enable_if_t<std::is_integral_v<T>, T>   // 只有 T 是整数时，这个返回类型才合法
safeDivide(T a, T b) {
    return a / b;
}
```

`enable_if_t<cond, T>` = `T`（当 cond 为 true），否则不存在——于是条件不满足时这个模板被 SFINAE 剔除。

### 7.3 `void_t` 技巧（C++17）

一个多用途检测器：**任何类型塞进 `void_t` 都变成 `void`；但如果表达式中途某一步非法，整个 `void_t` 就替换失败**。

```cpp
template<typename...> using void_t = void;

// 检测 T 是否有成员类型 value_type
template<typename T, typename = void>
struct has_value_type : std::false_type {};

template<typename T>
struct has_value_type<T, void_t<typename T::value_type>> : std::true_type {};

static_assert(has_value_type<std::vector<int>>::value);   // vector 有 value_type
static_assert(!has_value_type<int>::value);               // int 没有
```

> ⚠️ **坑**：`std::enable_if` 与 `void_t` 的可读性都很差。**C++20 之后新代码一律用 Concepts（第 8 节）**——SFINAE 技巧只需要"看得懂"：你会在老代码和 Boost 里经常撞见。

---

## 8. Concepts（C++20 ⭐）

Concepts 给模板参数"加要求"，失败时直接报出你的错误信息，而不是一屏模板展开。

```cpp
#include <concepts>

template<typename T>
concept Summable = requires(T a, T b) { a + b; };   // 要求 a+b 合法

template<Summable T>                      // 约束模板参数
T sum(T a, T b) { return a + b; }

template<typename T> requires Summable<T> // 等价写法
T sum2(T a, T b) { return a + b; }

template<typename T>
T sum3(T a, T b) requires Summable<T> {   // 尾置 requires
    return a + b;
}
```

使用：

```cpp
int i = sum(1, 2);          // OK
// sum(std::string("a"), std::string("b"));  // 注意：std::string + std::string 合法，能过
// sum(nullptr, nullptr);   // error: 约束未满足，报错信息指向 concept 名
```

标准库自带一堆现成 concept：`std::integral`、`std::floating_point`、`std::same_as`、`std::convertible_to`、`std::invocable`、`std::ranges::range` 等。

**Concepts 与 SFINAE 的对比**：

| | SFINAE / enable_if | Concepts |
|---|---|---|
| 标准 | C++11 | C++20 |
| 报错信息 | 模板展开一屏 | 点名"约束 `Summable` 未满足" |
| 可读性 | 差 | 好 |
| 重载优先级 | 靠偏序 | 更约束的 concept 更优先 |
| 是否产生库代码膨胀 | 有时 | 编译更快、更少 |

> 💡 你项目里的 `EventDispatcher::dispatch<T, F>` 其实不需要约束（它接受一切有 `getStaticType()` 的 T），但如果你要约束 F 必须返回 bool，C++20 下就该写
> `template<typename T, std::invocable<T&> F> bool dispatch(F&& func)`。

---

## 9. 完美转发（Perfect Forwarding）

### 9.1 问题：包装函数会改变实参的左右值性

```cpp
void target(std::string& s)  { /* 左值版本 */ }
void target(std::string&& s) { /* 右值版本 */ }

template<typename T>
void wrapper(T arg) {          // ❌ 按值传：std::string 被拷贝，右值被降级成左值
    target(arg);               // 永远调用左值版本
}
```

### 9.2 转发引用 + `std::forward`

```cpp
template<typename T>
void wrapper(T&& arg) {        // T&& 在模板推导里叫"转发引用"（万能引用）
    target(std::forward<T>(arg));  // 实参是右值 → 转回右值；是左值 → 保持左值
}
```

规则（背下来）：
- **转发引用必须是 `T&&` 且 T 是模板推导出来的**。`int&&` 就是普通右值引用，不是转发引用。
- 实参是左值 `x` → `T = X&`，折叠成 `X&`；实参是右值 → `T = X`，`X&&`。
- `std::forward<T>(arg)` 按 `T` 是左值引用还是右值引用决定把 `arg` 转回什么。
- 不转发的后果：右值被当成左值传给重载，可能多一次拷贝，甚至编译错误（移动独有的类型）。

标准库 `make_unique`、`emplace_back`、`std::invoke` 全靠这个。你在 [事件系统完整实现](01-事件系统完整实现.md) 里看到的 `dispatch<T>(std::forward<F>(func))` 也是同一招。

> ⚠️ **坑**：转发引用 + 重载会吸走一切（`wrapper(42)`、`wrapper(someLvalue)` 都匹配）。想限定只收右值，用 `std::move` 或 Concepts。

---

## 10. 模板的组织方式与性能取舍

### 10.1 定义必须可见（头文件原则）

模板**只有声明没有定义**时无法实例化。所以模板实现惯例放头文件（`.h/.hpp`），而不是 `.cpp`——这就是为什么你的 `EventDispatcher` 里 `dispatch<T>` 这类模板要写在头文件里，各 TU 才能各自实例化。

要控制二进制大小 / 隐藏实现，用**显式实例化**：

```cpp
// Foo.h
template<typename T>
T f(T);

// Foo.cpp
template<typename T> T f(T t) { return t * 2; }
template int f<int>(int);          // 只实例化 int 版本
template double f<double>(double);
```

其他 TU 只能用 `int` / `double` 两个版本。

### 10.2 三种"多态"的取舍

| 手段 | 分派时机 | 开销 | 灵活性 |
|---|---|---|---|
| **模板（静态分派）** | 编译期 | 零（可内联） | 编译期决定，类型必须可见 |
| **虚函数（动态分派）** | 运行期 | 一次间接跳转 + 不能内联 | 运行期决定，类型可隐藏 |
| **`std::function`（类型擦除）** | 运行期 | 虚调用 + 可能堆分配 | 能装任何可调用对象，最灵活 |

决策树：

- 类型在**编译期就知道**、性能敏感（数学运算、容器、事件分发）→ **模板**。
- 类型要**运行期切换**、数量有限（渲染后端、窗口平台层）→ **虚函数**。
- 要**存进容器 / 跨层传递任意回调**（GLFW 回调、事件订阅）→ **`std::function`**。

你项目的分工正符合这个决策树：`EventDispatcher::dispatch<T>` 用模板（编译期比类型、零开销、`static_cast` 安全），回调存储用 `std::function`（运行期才知道注册了什么）。详见 [`std::function` 完全指南](std-function完全指南.md)。

### 10.3 别过度设计

- 需要抽象但不需要性能 → 先虚函数，简单直接。
- 模板是传染的：一个模板函数会迫使它的调用链都是模板。公共 API 上慎用。
- 现代 C++ 里"元编程"首选 `constexpr`（§6.2），其次 Concepts，最后才是 §10.4 的老式类型递归。

### 10.4 老式类型级元编程（看懂即可）

C++11 时代的"编译期计算"靠模板递归 + 特化（和 §5.3 同构）：

```cpp
template<unsigned N>
struct Fib {
    static constexpr unsigned value = Fib<N-1>::value + Fib<N-2>::value;
};
template<> struct Fib<0> { static constexpr unsigned value = 0; };
template<> struct Fib<1> { static constexpr unsigned value = 1; };

static_assert(Fib<10>::value == 55);
```

对比 §6.2 的 `constexpr` 版：同样零运行期开销，但 `constexpr` 可读性碾压。**新代码写 `constexpr`，别写这个。**

---

## 🔧 练习

> 答案在下方 📝 参考答案。**先写，跑通，再对答案**。每题都标了对应的章节和验证点。

1. **通用 min/max**（§2）：写 `mymin` / `mymax` 函数模板，用 `const T&`，至少支持 `int`、`double`、`std::string` 三种调用，各验证一次。为什么这里不能直接用 `T` 按值返回后比较 `std::string`？顺带说明。

2. **Stack 类模板**（§3）：用 `std::vector` 实现 `Stack<T>`，含 `push` / `pop` / `top` / `size` / `empty`。`pop()` 要求返回 `void`（为什么？见答案）。用 `Stack<int>` 和 `Stack<std::string>` 各验证一轮 push/pop。

3. **FixedArray**（§3 非类型参数）：实现 `FixedArray<T, N>`：`operator[]`（含 const 版本）、`size()` 返回 N、`fill(const T&)` 方法。验证：用 `constexpr` 对象在 `static_assert` 里调用 `size()`（证明 N 是编译期常量）；`fill` 后所有元素相等。

4. **变参 sum**（§5）：写 `sumAll(args...)`，支持任意个数的算术参数；**空调用 `sumAll()` 必须合法且返回 0**。验证 `sumAll(1,2,3)==6`、`sumAll()==0`、`sumAll(1.5, 2.5)==4.0`。

5. **if constexpr 打印**（§6）：写 `print(const T& v)`：有 `begin()`/`end()` 的（容器）打印元素列表 `[1, 2, 3]`，否则直接打印 `v`。验证 `print(std::vector{1,2,3})`、`print(42)`、`print(std::string("hi"))`。提示：用 §7.3 的 `void_t` 写 `has_begin_end` 检测器，或者直接 `if constexpr` + `decltype`。

6. **has_foo 检测器**（§7）：用 `void_t` 写 `has_foo<T>`，检测 T 是否能调用 `foo(int)`。验证：有 `foo(int)` 的类为 true、`int` 为 false。再想：只有 `foo(double)` 的类会得到什么结果？为什么？（提示：隐式转换）

7. **enable_if 重载**（§7）：写 `describe(T v)` 的两个重载——整数版本输出 `"integer"`，其他版本输出 `"other"`，用 `enable_if` 实现，`describe(42)` / `describe(3.5)` / `describe("hi")` 各验证。

8. **完美转发包装器**（§9）：写 `wrapper(T&& arg)`，转发到 `use(std::string&)` / `use(std::string&&)` 两个重载（各自打印），验证 `wrapper(std::string("rvalue"))` 调右值版、左值变量调左值版。**再写一个不转发（按值 `T arg`）的对照组**，证明它永远调左值版。

9. **Concept 约束**（§8, C++20）：定义 concept `Addable`（要求 `a + b` 合法），用它约束 `add(a, b)`。用 `static_assert` 验证 concept 对 `int` 满足、对 `nullptr_t` 不满足；调用 `add(1,2)` 成功。**（编译用 `/std:c++20`）**

10. **constexpr 斐波那契**（§6/§10）：写 `constexpr unsigned fib(unsigned n)`，用 `static_assert` 验证 `fib(0)==0`、`fib(1)==1`、`fib(10)==55`。再给出它对应的运行期循环版本，证明两者输出一致。

---

## 📝 参考答案

> 全部答案在 MSVC 19.44（`/std:c++17`，第 9 题 `/std:c++20`）下编译运行通过。
> 每题复制成独立 `.cpp` 即可运行；练习 4/5/8 依赖上题的检测器时已内联进答案。
> 💡 MSVC 编译含中文注释的源码建议加 **`/utf-8`** 标志，否则 UTF-8 注释可能触发 C4819 警告，个别情况还会误吞代码行。

### 1. 通用 min/max

```cpp
#include <iostream>
#include <string>

template<typename T>
const T& mymin(const T& a, const T& b) { return b < a ? b : a; }

template<typename T>
const T& mymax(const T& a, const T& b) { return a < b ? b : a; }

int main() {
    std::cout << mymin(3, 5) << ' ' << mymax(3, 5) << '\n';                     // 3 5
    std::cout << mymin(2.5, 1.25) << ' ' << mymax(2.5, 1.25) << '\n';           // 1.25 2.5
    std::string a = "apple", b = "banana";
    std::cout << mymin(a, b) << ' ' << mymax(a, b) << '\n';                     // apple banana
}
```

要点：`const T&` 版本对 `std::string` **零拷贝**。如果写 `T mymin(T a, T b)`，每次调用会拷贝两个 `std::string`（各一次堆分配）。返回 `const T&` 还避免了返回值拷贝。

### 2. Stack 类模板

```cpp
#include <iostream>
#include <string>
#include <vector>

template<typename T>
class Stack {
public:
    void push(const T& v) { m_data.push_back(v); }
    void pop()             { m_data.pop_back(); }
    const T& top() const   { return m_data.back(); }
    bool empty() const     { return m_data.empty(); }
    std::size_t size() const { return m_data.size(); }
private:
    std::vector<T> m_data;
};

int main() {
    Stack<int> s;
    s.push(1); s.push(2); s.push(3);
    std::cout << s.size() << ' ' << s.top() << ' ' << s.empty() << '\n';   // 3 3 0
    s.pop();
    std::cout << s.size() << ' ' << s.top() << '\n';                        // 2 2

    Stack<std::string> ss;
    ss.push("hello"); ss.push("world");
    std::cout << ss.top() << ' ' << ss.size() << '\n';                      // world 2
}
```

为什么 `pop()` 返回 `void`：返回 `T` 需要先拷贝/移动再删除，空栈时还无值可返；标准库 `std::stack` 同样如此（异常安全设计）。取元素请 `top()` 在前、`pop()` 在后。C++11 起可加 `T&& pop_value()` 变体，但默认接口保持 `void`。

### 3. FixedArray

```cpp
#include <cassert>
#include <cstddef>
#include <iostream>

template<typename T, std::size_t N>
class FixedArray {
public:
    constexpr FixedArray() : m_data{} {}   // 值初始化清零 → constexpr 对象才可常量初始化
    T& operator[](std::size_t i) { assert(i < N); return m_data[i]; }
    const T& operator[](std::size_t i) const { assert(i < N); return m_data[i]; }
    constexpr std::size_t size() const { return N; }
    void fill(const T& v) { for (std::size_t i = 0; i < N; ++i) m_data[i] = v; }
private:
    T m_data[N];
};

int main() {
    FixedArray<float, 4> vec4;
    vec4.fill(1.5f);
    std::cout << vec4.size() << ' ' << vec4[0] << ' ' << vec4[3] << '\n';   // 4 1.5 1.5
    vec4[2] = 9.0f;
    std::cout << vec4[2] << '\n';                                            // 9

    // N 是编译期常量：constexpr 对象 + constexpr 成员函数 → 编译期求值
    constexpr FixedArray<float, 4> carr{};
    static_assert(carr.size() == 4);
    constexpr std::size_t n = carr.size();                                   // 显式证明编译期
    static_assert(n == 4);
}
```

说明：`size()` 是成员函数，必须通过对象调用（`FixedArray<float,4>::size()` 这种写法对非静态成员是非法语法）。它要能在 `static_assert` 里求值，需要三件事：**对象本身是 `constexpr`**、**`size()` 标记了 `constexpr`**、**构造是常量初始化**（所以加了 `constexpr FixedArray() : m_data{} {}`——否则数组成员保持未初始化，不满足常量表达式要求）。`vec4[2] = 9.0f` 走非 const `operator[]`，const 版本给 `const FixedArray` 用。

### 4. 变参 sum

```cpp
#include <iostream>

template<typename... Ts>
auto sumAll(Ts... args) {
    return (args + ... + 0);   // 带初值右折叠：空包返回 0
}

int main() {
    std::cout << sumAll(1, 2, 3) << '\n';        // 6
    std::cout << sumAll() << '\n';               // 0
    std::cout << sumAll(1.5, 2.5) << '\n';       // 4
    std::cout << sumAll(10) << '\n';             // 10
}
```

`(args + ... + 0)` 展开成 `1 + (2 + (3 + 0))`，初值 `0` 兜底空包。注意 `sumAll(1, 2.5)` 会得到 `3.5`（int 提升为 double），类型取 `auto` 由表达式决定。

### 5. if constexpr 打印

```cpp
#include <iostream>
#include <string>
#include <vector>

// §7.3 的 void_t 检测器：有没有 begin()/end() 成员
template<typename...> using void_t = void;

template<typename T, typename = void>
struct has_begin_end : std::false_type {};
template<typename T>
struct has_begin_end<T, void_t<decltype(std::declval<T>().begin()),
                               decltype(std::declval<T>().end())>> : std::true_type {};

template<typename T>
void print(const T& v) {
    if constexpr (has_begin_end<T>::value) {
        std::cout << '[';
        bool first = true;
        for (const auto& e : v) {
            if (!first) std::cout << ", ";
            first = false;
            std::cout << e;
        }
        std::cout << "]\n";
    } else {
        std::cout << v << '\n';
    }
}

int main() {
    print(std::vector<int>{1, 2, 3});   // [1, 2, 3]
    print(42);                          // 42
    print(std::string("hi"));           // [h, i]（string 也有 begin/end，命中容器分支）
}
```

关键点：`if constexpr` 的 else 分支在容器分支被选中时**不实例化**——所以 `std::cout << v` 对 `std::vector` 永远不编译。坑点：`std::string` 同时有 `begin()` 且可 `<<`，但 `if constexpr` 分支在编译期决定，**先命中容器分支**，输出 `[h, i]` 而非 `hi`。想让它走"直接打印"分支，得把容器检测条件改成"有 begin/end **且不是** `std::string`"，或直接特化处理字符串——本练习按"有 begin/end 一律按容器"处理。

### 6. has_foo 检测器

```cpp
#include <iostream>
#include <type_traits>
#include <utility>

template<typename...> using void_t = void;

template<typename T, typename = void>
struct has_foo : std::false_type {};
template<typename T>
struct has_foo<T, void_t<decltype(std::declval<T>().foo(std::declval<int>()))>> : std::true_type {};

struct WithFoo  { int foo(int) { return 1; } };
struct WrongSig { double foo(double) { return 2; } };

int main() {
    std::cout << has_foo<WithFoo>::value  << '\n';   // 1
    std::cout << has_foo<int>::value      << '\n';   // 0
    std::cout << has_foo<WrongSig>::value << '\n';   // 1（见下）
}
```

`std::declval<int>()` 制造一个"假装存在的 int 实参"（`declval` 只能在未求值上下文里用，这里正好在 `decltype` 里）。`WithFoo.foo(int)` 精确匹配 → true；`int` 没有成员 `foo` → 替换失败 → false。

> ⚠️ **坑（本练习的隐藏考点）**：`WrongSig` 的 `foo(double)` 会**隐式接受 int**（int→double 是标准隐式转换），所以检测结果是 **true**，不是 false。`0` 字面量同理。想检测"参数必须精确匹配 `int`"需要签名级 SFINAE（超出本练习范围）——`void_t` 检测的是"能不能这么调"，不是"签名完全一致"。这正是练习 6 想让你踩的坑。

### 7. enable_if 重载

```cpp
#include <iostream>
#include <type_traits>

template<typename T>
std::enable_if_t<std::is_integral_v<T>>
describe(T v) { std::cout << "integer\n"; }

template<typename T>
std::enable_if_t<!std::is_integral_v<T>>
describe(T v) { std::cout << "other\n"; }

int main() {
    describe(42);      // integer
    describe(3.5);     // other
    describe("hi");    // other
}
```

两个模板互为 SFINAE 开关：整数时只有第一个启用，非整数时只有第二个启用，**永远不会同时启用**（否则重载歧义）。注意 `describe("hi")` 的 `T` 推导为 `const char*`，不是 `std::string`。

### 8. 完美转发包装器

```cpp
#include <iostream>
#include <string>
#include <utility>

void use(std::string& s)  { std::cout << "lvalue: " << s << '\n'; }
void use(std::string&& s) { std::cout << "rvalue: " << s << '\n'; }

template<typename T>
void wrapper(T&& arg) {
    use(std::forward<T>(arg));
}

template<typename T>
void badWrapper(T arg) {          // 对照组：按值接收
    use(arg);
}

int main() {
    wrapper(std::string("rvalue"));   // rvalue: rvalue
    std::string s = "lvalue";
    wrapper(s);                       // lvalue: lvalue（左值变量）

    badWrapper(std::string("rvalue")); // lvalue: rvalue（右值被降级成左值，调错重载）
}
```

运行结果三行：

```
rvalue: rvalue
lvalue: lvalue
lvalue: rvalue
```

对照组证明：`T&&` 转发保住了实参的左右值性；按值参数 `T arg` 会创建局部副本，永远是左值。性能上：不转发意味着 `std::string` 多一次拷贝（或移动，取决于调用方）；语义上：移动独有的类型（如 `std::unique_ptr`）不转发会直接编译失败。

### 9. Concept 约束（C++20）

```cpp
// 编译：cl /std:c++20 /EHsc answer9.cpp
#include <concepts>
#include <iostream>

template<typename T>
concept Addable = requires(T a, T b) { a + b; };

template<Addable T>
T add(T a, T b) { return a + b; }

int main() {
    std::cout << add(1, 2) << '\n';            // 3
    std::cout << add(1.5, 2.5) << '\n';        // 4

    static_assert(Addable<int>);               // int 可相加
    static_assert(!Addable<std::nullptr_t>);   // nullptr_t 不可相加

    // 编译期验证约束生效：下面这行取消注释会报错，且错误信息直接点名 Addable
    // add(nullptr, nullptr);
}
```

`requires(T a, T b) { a + b; }` 是 requires 表达式：`a+b` 合法则该 concept 满足。`static_assert(Addable<int>)` 编译期验证 concept 的布尔值。C++17 下这段编译不过（`concept` 是 C++20 关键字）。

### 10. constexpr 斐波那契

```cpp
#include <cassert>
#include <iostream>

constexpr unsigned fib(unsigned n) {
    return n < 2 ? n : fib(n - 1) + fib(n - 2);
}

unsigned fib_runtime(unsigned n) {   // 运行期循环版
    unsigned a = 0, b = 1;
    for (unsigned i = 0; i < n; ++i) { unsigned t = a + b; a = b; b = t; }
    return a;
}

int main() {
    static_assert(fib(0) == 0);
    static_assert(fib(1) == 1);
    static_assert(fib(10) == 55);

    for (unsigned n = 0; n <= 20; ++n)
        assert(fib(n) == fib_runtime(n));   // 两个版本完全一致
    std::cout << "fib(10) = " << fib(10) << '\n';   // 55
}
```

`static_assert` 证明 `fib` 在**编译期**被求值（`constexpr` 函数在常量表达式上下文中必然编译期执行）。运行期断言循环版与 constexpr 版一致——两个实现、同一语义，这是"编译期与运行期结果等价"的标准验证手法。

---

## 参考与延伸阅读

- **cppreference — Function template**：<https://en.cppreference.com/w/cpp/language/function_template>（实例化、推导、显式实参的权威定义）
- **cppreference — Pack (since C++11)**：<https://en.cppreference.com/w/cpp/language/pack>（参数包、展开、fold）
- **cppreference — Class template**：<https://en.cppreference.com/w/cpp/language/class_template>
- **cppreference — Constraints and concepts (C++20)**：<https://en.cppreference.com/w/cpp/language/constraints>
- **cppreference — SFINAE**：<https://en.cppreference.com/w/cpp/language/sfinae>
- **Modernes C++ — Template Metaprogramming: How it All Started**：<https://www.modernescpp.com/index.php/template-metaprogramming-a-introduction/>（元编程历史与动机）
- **dr_strangetemplate（GitHub）**：<https://github.com/MCGallaspy/dr_strangetemplate>（工程视角的模板使用指南："templated code is type-safe, expressive, and can shift computation to compile-time"）
- **CppCon 2022 — Kris Jusiak《C++20 - A New Way of Meta-Programming?》**：<https://www.youtube.com/watch?v=zRYlQGMdISI>（C++20 之后元编程的新范式）
- 本系列相关：[`std::function` 完全指南](std-function完全指南.md)（模板的对立面：类型擦除）、[事件系统设计](01-事件系统设计.md)（`dispatch<T>` 实战）
