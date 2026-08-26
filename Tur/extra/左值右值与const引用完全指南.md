# Extra · 左值、右值与 const& 完全指南

> 目标：你的 [事件系统](../advanced/01-事件系统设计.md) 里，`dispatch(const F& func)` 为什么用 `const&`，
> `setEventCallback(EventCallback cb) { m_eventCb = std::move(cb); }` 为什么按值收再 `std::move`？
> 本文从"什么能取地址"讲到 C++11 值类别全景、四种引用的绑定规则、临时对象的生命周期延长、
> 移动语义与完美转发。全部 7 道练习均附编译运行验证过的参考答案。

> 📌 前置：看得懂基础 C++（类、引用、`const`、`std::vector`）。代码基于 **C++17**，
> 全部在本仓库环境（MSVC / `/std:c++17 /utf-8`）编译运行通过。

---

## 1. 左值与右值：先建立直觉

**一句话：左值是"还在那儿的东西"，右值是"马上要没的东西"。**

判定口诀（覆盖 99% 场景）：**能对表达式取地址（`&expr` 编译通过）→ 左值；否则 → 右值。**

```cpp
int a = 10;
&a              // ✅ a 是左值：有名字、有地址、这条语句结束后还在
&10             // ❌ 10 是右值：字面量，没有可取的地址
&(a + 1)        // ❌ a + 1 是右值：结果放在临时位置，下一条语句就没了
&makeString()   // ❌ 按值返回的临时对象是右值（makeString 按值返回）
```

三个高频误区：

- **左右值是"表达式"的属性，不是"变量"的属性**。`std::string s;` 声明的 `s` 是个对象，但值类别永远针对**表达式**判断：`s` 是左值，`s + "x"` 是右值。
- **有名字的右值引用变量，本身是左值**。`void f(std::string&& x)` 里 `x` 的类型是右值引用，但 `x` 有名字、可取地址——`x` 这个表达式是左值。这是完美转发的全部难点（§7）。
- **字符串字面量是左值**。`"hello"` 的类型是 `const char[6]`，放在静态存储区，`&"hello"` 合法；而 `42`、`3.14` 是右值。

> 💡 引擎程序员为什么在乎？因为**右值是"可以被偷"的对象**：它马上要销毁，把它的堆内存、句柄直接搬走（移动）比复制一份便宜得多。`std::function`、纹理对象、顶点缓冲——引擎里到处是"搬家比盖新房便宜"的场景。

## 2. C++11 值类别全景

C++11 把"非左值"细分了。标准里每个表达式属于且仅属于以下三类之一：

```
                表达式
               ┌──────┴────────┐
           glvalue            rvalue
         （有身份）         （可被移动）
          ┌──┴───┐          ┌──┴───┐
       lvalue   xvalue ─── xvalue  prvalue
                （xvalue 同属两边）
```

| 类别 | 全称 | 一句话 | 例子 |
|---|---|---|---|
| **lvalue** | left value | 有身份，还能继续用 | `x`、`v[0]`、`"literal"`、`*p` |
| **prvalue** | pure rvalue | 纯临时，没名字 | `42`、`x + 1`、`std::string("t")`、按值返回的 `foo()` |
| **xvalue** | expiring value | 有身份，但被标记为"可掏空" | `std::move(x)`、`static_cast<T&&>(x)` |

两个组合词：glvalue = lvalue + xvalue（**有身份**）；rvalue = prvalue + xvalue（**可被移动**）。

> 💡 日常说"右值"时指的是 rvalue = prvalue + xvalue，本文沿用这个习惯。

`std::move` 是理解 xvalue 的钥匙——**它不移动任何东西**，只是 `static_cast<T&&>(x)`：把左值表达式转换成 xvalue，向重载决议声明"这个对象我不想再用了，谁想搬就搬"。真正的"移动"发生在移动构造/移动赋值里（§6）。

## 3. 四种引用的绑定规则

| 引用类型 | 绑左值 | 绑 const 左值 | 绑右值 | 典型用途 |
|---|:---:|:---:|:---:|---|
| `T&` | ✅ | ❌ | ❌ | 我要在函数里**修改**你 |
| `const T&` | ✅ | ✅ | ✅ | 我只**读**你（零拷贝 + 什么都收） |
| `T&&` | ❌ | ❌ | ✅ | 我要**掏空**你（移动） |
| `const T&&` | ❌ | ❌ | ✅ | 几乎不用（const 挡住移动） |

> ⚠️ 非常量左值引用 `T&` 绑不了右值是**故意的**：若 `void f(int& x); f(40);` 合法，函数对临时对象的修改无声消失，无人观察得到——纯浪费还误导。`const T&` 绑右值没这个问题：反正只读。

### 3.1 重载决议：右值优先 `T&&`，左值只进 `const&`

`const T&` 和 `T&&` 同时存在时，编译器按实参的值类别分流：

```cpp
void probe(const std::string&) { std::cout << "const&\n"; }
void probe(std::string&&)      { std::cout << "&&\n"; }

std::string s = "lvalue";
probe(s);                     // 左值   → const&
probe(std::string("rvalue")); // 纯右值 → &&
probe(std::move(s));          // xvalue → &&
```

实测输出：

```text
probe(const std::string&)
probe(std::string&&)
probe(std::string&&)
```

规则：**右值绑定 `T&&` 优于 `const T&`**（更优的隐式转换序列）；左值根本无法绑定 `T&&`，只能进 `const&`。这一条就是移动语义的入口。

## 4. `const T&` 绑右值：为什么 + 生命周期延长

### 4.1 为什么允许

假如 `const&` 不能绑右值，这些全写不了：

```cpp
std::string s = s1 + s2 + s3;          // s1+s2 的中间结果是临时右值
window.setEventCallback([](Event& e) { /* ... */ });  // lambda 表达式是纯右值
void dispatch(const F& func);          // 若只收左值，临时可调用物进不来
```

更隐蔽的一层：**隐式转换产生的临时**。

```cpp
void greet(const std::string& name);
greet("world");   // "world" 是 const char[6]，先构造临时 std::string，再绑到 const&
```

没有这条规则，传字面量、传 `const char*`、传任何可转换类型都得手动构造，接口会难用到没法用。

### 4.2 生命周期延长（lifetime extension）

**规则：`const T&`（和 `T&&`）直接绑定临时对象时，临时的生命周期延长到引用的作用域结束。**

```cpp
struct Tracer {
    const char* name;
    Tracer(const char* n) : name(n) { std::cout << "    construct " << name << '\n'; }
    ~Tracer()                       { std::cout << "    destruct   " << name << '\n'; }
};

{
    const Tracer& r = Tracer("bound-to-const&");
    std::cout << "    (still in scope)\n";
}   // ← 临时在这里才析构，而不是它所在的那条语句
std::cout << "    (left scope)\n";
```

实测输出：

```text
    construct bound-to-const&
    (still in scope)
    destruct   bound-to-const&
    (left scope)
```

C++17 起这条规则还穿透**成员访问链**（实测验证）：

```cpp
const Tracer& r = Holder("via-member").inner;   // .inner 是纯右值 Holder 的成员
                                                // → 绑定延长的是整个 Holder
```

> ⚠️ **延长只对"直接绑定"生效**。凡是隔了一层函数（函数返回引用），延长就不适用——这是 §8 头号悬垂来源。

## 5. 函数参数怎么选：值 / `const&` / `T&&`

| 场景 | 写法 | 例子 |
|---|---|---|
| 小对象（≤ 2 个指针大小、拷贝平凡） | **按值** `T x` | `int`、`float`、`bool`、指针、`glm::vec2/3/4` |
| 只读的大对象 | **`const T&`** | `const std::string&`、`const glm::mat4&` |
| 要存下来 / 独占（sink） | **按值 + `std::move`** 或 `T&&` | `setEventCallback(EventCallback cb) { m_eventCb = std::move(cb); }` |
| 完美转发（模板语境） | `T&&` + `std::forward` | `template<typename F> void wrapper(F&& f)` |

你仓库里的分工正是教科书式的（[进阶 01](../advanced/01-事件系统设计.md)）：

```cpp
template<typename T, typename F>
bool dispatch(const F& func) { /* 只调用、不存储 → const& */ }

void setEventCallback(EventCallback cb) { m_eventCb = std::move(cb); }  // 要存储 → 按值 + move
```

**sink 为什么"按值 + move"而不是 `const&` + 拷贝？** 用带日志的 `Filter` 实测计数（完整程序见练习 5）：

```cpp
std::vector<Filter> store;
void addVal(const Filter& f) { store.push_back(f); }           // 左值实参：1 拷贝
void addSink(Filter f)       { store.push_back(std::move(f)); } // 左值实参：1 拷贝 + 1 移动
                                                                // 右值实参：1 移动（见下）
```

调用 `addSink(Filter("c"))` 时：`Filter("c")` 是纯右值，**直接物化**为形参 `f`（C++17 保证的拷贝省略，零开销），再移动进 `store`——总共 1 ctor + 1 move。而 `const&` 版本对右值也强制走拷贝。对可能带堆分配的 `std::function`，这个差距是真金白银的。

> 💡 按值 sink 的口诀：**"一份拷贝换两种调用都够用"**——左值调用者付 1 拷 + 1 移，右值调用者只付 1 移。不想让左值付那次拷贝，才上 `T&&` 重载（或模板 + forward，§7）。

## 6. 移动语义：`std::move` 与移动构造

**`std::move(x)` 一个字节都不移动**——它就是 `static_cast<std::remove_reference_t<T>&&>(x)` 的花括号，作用只有一个：把左值表达式变成 xvalue，从而在重载决议里选中**移动**版本。

```cpp
std::string a = "0123456789abcdefghij";   // 20 字符，超出 SSO，堆分配
std::string b = a;                        // 拷贝构造：深复制一份堆缓冲
std::string c = std::move(a);             // 移动构造：把 a 的堆指针偷给 c
```

实测输出（MSVC）：

```text
a.size() = 0      ← 移动后的源对象处于"有效但未指定"状态（MSVC 上观察到为空）
b.size() = 20
c.size() = 20
```

要点：

- 移动后的源对象**状态未指定**（标准只保证可析构、可赋新值）。别再读它，只能重新赋值或销毁。
- 自写移动构造请标 **`noexcept`**：`std::vector` 扩容只有对 `noexcept` 移动才敢搬旧元素，否则退回拷贝（强异常安全保证）。
- `EventCallback = std::function<void(Event&)>` 拷贝可能堆分配、移动只是几个指针——这就是 §5 的 sink 要 move 的原因。

> ⚠️ 返回 `const T`（按值）会挡住移动：`const std::string make(); std::string s = std::move(make());` 里返回的 const 临时没法绑 `T&&`，只能拷贝。按值返回就写裸 `T`。

## 7. 万能引用与完美转发

§3 说"`T&&` 只绑右值"——有个著名例外。**在模板推导 / `auto&&` 语境里，`T&&` 是万能引用（forwarding reference）**，左右值都能绑：

```cpp
template<typename T>
void wrapper(T&& x);     // 万能引用：T 同时在被推导

void f(Widget&& w);      // 不是万能引用：Widget 是具体类型，没有推导
```

机制是**引用折叠**（reference collapsing）：

| T 推导为 | `T&&` 折叠为 | 实参是 |
|---|---|---|
| `Widget&` | `Widget& &&` → **`Widget&`** | 左值 |
| `Widget` | `Widget&&` | 右值 |

四条折叠规则一句话：**只要出现左值引用，结果就是左值引用；两个右值引用叠起来才是右值引用**。

但绑进来只是第一步。回忆 §1 误区二：**有名字的变量是左值**——`wrapper(T&& x)` 内部，`x` 是左值！直接 `use(x)` 全部走 `const&` 版本。要保真传递，用 `std::forward`：

```cpp
void use(const std::string&) { std::cout << "  const&\n"; }
void use(std::string&&)      { std::cout << "  &&\n"; }

template<typename T>
void good(T&& x) { use(std::forward<T>(x)); }   // 万能引用 + forward：保真

template<typename T>
void bad(T x)    { use(x); }                    // 对照组：按值收，x 永远是左值
```

实测输出：

```text
good(s):                const&    ← 左值保真
good(std::string("r")): &&        ← 右值保真
bad(s):                 const&
bad(std::string("r")):  const&    ← 右值被降级成左值，偷不成了
```

> 💡 记忆：`std::move` = **无条件**转右值（我不想再用了）；`std::forward<T>` = **有条件**转右值（原来是什么就传什么）。两者都只是类型转换，本身不生成代码。
> 模板推导语境的更多细节见 [C++ 模板完全指南](Cpp模板完全指南.md) §9 完美转发。

## 8. 常见坑：悬垂引用

### 8.1 `const int& r = std::max(1, 2)` —— 经典 UB

`std::max` 的签名是 `template<class T> const T& max(const T&, const T&)`——**返回的是引用**。`1`、`2` 作为右值实参产生的临时在整条语句结束时析构；延长规则**不适用**（`r` 绑的不是临时本身，而是函数返回的引用）。用同构的自定义函数复现（完整程序见练习 4）：

```cpp
struct N { ~N() { std::cout << "    ~N\n"; } };
const N& pick(const N& a, const N& b) { return b; }   // 和 std::max 同构

const N& r = pick(N{}, N{});
```

```text
  before
    ~N      ← 两个临时在语句末尾析构
    ~N
  after     ← r 已悬垂，再解引用即 UB
```

修复：**按值接收** `int r = std::max(1, 2);`。凡是接收"返回 `const T&` 的函数"的结果，一律优先考虑按值。

### 8.2 range-for 里的临时对象

```cpp
struct Wrapper { std::vector<int> v; const std::vector<int>& data() const { return v; } };
Wrapper makeW();

for (int x : makeW().data())   // ❌ C++23 前悬垂：__range 绑的是 data() 返回的引用，
                               //    makeW() 的临时在进入循环体前就析构了
for (int x : makeW().v)        // ✅ 成员访问链上的临时被延长（§4.2 同款规则）
```

C++23（[P2718R0](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p2718r0.html)）才把 range-for **初始化表达式里所有临时**的生命周期延长到循环结束；C++17 只保护"直接物化给 `__range` 的那一个"。跨版本最稳的写法：先把临时存成具名变量再遍历。

### 8.3 返回局部变量的引用/指针

```cpp
const std::string& name() {
    std::string local = "x";
    return local;             // ❌ 悬垂
}
```

按值返回就好：`return local;` 从 C++11 起把 `local` 隐式当作右值（有机会走移动构造），多数编译器还做 NRVO 直接原地构造——按值返回在主流实现里就是零开销。

### 8.4 `const&` 成员禁用赋值

```cpp
struct Config { const std::string& name; };   // 引用成员 → 拷贝赋值运算符被隐式删除
```

引用成员还绑死了初始化时的对象，无法改指。要"只读视图"语义用 `std::string_view`（C++17），要所有权就直接存值。

## 9. 一页总结

口诀：**`const T&` = 只读 + 绑一切 + 零拷贝；`T&` = 我要改你；`T&&` = 我要掏空你。**

三张表就是本篇全部：

- 四种引用的绑定规则 → §3
- 参数传递决策（值 / `const&` / `T&&`）→ §5
- 生命周期延长 + 三条例外（函数返回引用、range-for 旧规则、返回局部引用）→ §4.2、§8

---

## 📝 练习

**练习 1（值分类）** 判断以下表达式的值类别（lvalue / prvalue / xvalue）：

```cpp
std::string s;  std::vector<int> v{1, 2};
① s         ② s + "b"          ③ 42                ④ std::move(s)
⑤ "literal" ⑥ std::string("t") ⑦ v[0]              ⑧ v.begin()
```

**练习 2（改错）** `void greet(std::string& name);` 调用 `greet("world")` 编译失败。给出两种修法，并说明调用者传左值时的差异。

**练习 3（重载预言）** §3.1 的 `probe` 两个重载，先纸面预测三行调用各命中哪个，再编译验证。

**练习 4（找悬垂）** `const int& r = std::max(1, 2);` 编译通过但运行 UB。解释机制并给出修复。

**练习 5（sink 成本）** 用带日志的 `Filter`（ctor/copy/move 都打印）对比 `addVal(const Filter&)`、`addSink(Filter)`（按值 + move）、`addRref(Filter&&)` 三种"存进 vector"实现的拷贝/移动次数。

**练习 6（生命周期）** 用析构打日志的 `Tracer` 预测三段代码的输出：① `const Tracer& r = Tracer("T1");`；② `const Tracer& r = identity(Tracer("T2"));`（`identity` 返回 `const Tracer&`）；③ `Tracer("T3");`（丢弃表达式）。

**练习 7（完美转发）** 实现 `good(T&& x) { use(std::forward<T>(x)); }` 与对照组 `bad(T x)`，验证左右值的保真与降级。

---

## 参考答案

> 编译命令：`cl /std:c++17 /EHsc /utf-8 文件.cpp`（`/utf-8` 必加，否则中文注释在 GBK 代码页下会吞掉换行、注释掉下一行代码）。

### 1. 值分类

答案：① lvalue ② prvalue ③ prvalue ④ xvalue ⑤ **lvalue**（字符串字面量！类型 `const char[8]`，在静态存储区）⑥ prvalue ⑦ lvalue（`operator[]` 返回 `int&`）⑧ prvalue（按值返回迭代器）。

用 `decltype((expr))` 让编译器替你验证——左值得 `T&`，xvalue 得 `T&&`，prvalue 得裸 `T`（`decltype` 的操作数不求值，`static_assert` 里也能写运行期对象）：

```cpp
static_assert(std::is_lvalue_reference_v<decltype((s))>);              // ①
static_assert(!std::is_reference_v<decltype((s + "b"))>);              // ②
static_assert(!std::is_reference_v<decltype((42))>);                   // ③
static_assert(std::is_rvalue_reference_v<decltype((std::move(s)))>);   // ④
static_assert(std::is_lvalue_reference_v<decltype(("literal"))>);      // ⑤
static_assert(!std::is_reference_v<decltype((std::string("t"))) >);    // ⑥
static_assert(std::is_lvalue_reference_v<decltype((v[0]))>);           // ⑦
static_assert(!std::is_reference_v<decltype((v.begin()))>);            // ⑧
```

程序输出：`all 8 assertions hold`。

### 2. 改错

```cpp
void greetRef(const std::string& name) { std::cout << "const&: " << name << '\n'; }  // 修法一
void greetVal(std::string name)        { std::cout << "value : " << name << '\n'; }  // 修法二

greetRef("world");   // 临时 std::string 绑到 const&，函数内零拷贝
greetVal("world");   // 临时直接物化为形参（C++17 保证省略），同样零拷贝
```

实测输出：

```text
const&: world
const&: repo
value : world
value : repo
```

差异在调用者传**左值**时：`const&` 零拷贝，按值多一次拷贝构造。短字符串无所谓；大对象（`std::string` 内容长、矩阵、缓冲区）用 `const&`。

### 3. 重载预言

见 §3.1：左值 → `const&`；纯右值 → `&&`；`std::move(s)` → `&&`。实测输出与预言一致。

### 4. 悬垂机制

`std::max(const T&, const T&)` 返回引用，指向形参 `b`（这里是 `2` 对应的右值临时）。临时生命周期到整条语句结束——延长规则只对"直接绑定临时"生效，隔了函数返回就不适用。`r` 拿到的是指向已析构临时的引用。

修复（按值接收）：

```cpp
int r = std::max(1, 2);   // ✅ 值语义，谁也悬垂不了
```

复现程序（证明析构时机在语句末尾）：

```cpp
#include <iostream>

struct N { ~N() { std::cout << "    ~N\n"; } };
const N& pick(const N& a, const N& b) { return b; }

int main() {
    std::cout << "  before\n";
    const N& r = pick(N{}, N{});
    std::cout << "  after (r 已悬垂，再解引用即 UB)\n";
    (void)r;
}
// 输出：
//   before
//     ~N
//     ~N
//   after (r 已悬垂，再解引用即 UB)
```

### 5. sink 成本

```cpp
struct Filter {
    std::string tag;
    explicit Filter(const char* t) : tag(t)      { std::cout << "  ctor  " << tag << '\n'; }
    Filter(const Filter& o) : tag(o.tag)         { std::cout << "  copy  " << tag << '\n'; }
    Filter(Filter&& o) noexcept : tag(std::move(o.tag)) { std::cout << "  move  " << tag << '\n'; }
};

std::vector<Filter> store;
void addVal(const Filter& f) { store.push_back(f); }           // 只会拷贝
void addSink(Filter f)       { store.push_back(std::move(f)); }
void addRref(Filter&& f)     { store.push_back(std::move(f)); }
```

实测输出（每组先 `store.clear()`）：

```text
-- addVal(左值) --
  ctor  a
  copy  a                ← 1 拷贝
-- addSink(左值) --
  ctor  b
  copy  b
  move  b                ← 1 拷贝 + 1 移动
-- addSink(右值) --
  ctor  c
  move  c                ← 1 移动：纯右值直接物化为形参（C++17 省略），无入参开销
-- addRref(右值) --
  ctor  d
  move  d                ← 1 移动，但左值实参根本进不来
```

结论：`addSink` 对右值调用者和 `addRref` 一样便宜，对左值多一次移动（通常远比拷贝便宜），接口却只有一份。这就是仓库里 `setEventCallback(EventCallback cb) { m_eventCb = std::move(cb); }` 的选型依据。

### 6. 生命周期

```cpp
#include <iostream>

struct Tracer {
    const char* name;
    Tracer(const char* n) : name(n) { std::cout << "    construct " << name << '\n'; }
    ~Tracer()                       { std::cout << "    destruct   " << name << '\n'; }
};

const Tracer& identity(const Tracer& t) { return t; }

int main() {
    std::cout << "  [1] 直接绑定临时\n";
    { const Tracer& r = Tracer("T1"); std::cout << "    in scope\n"; }
    std::cout << "  [2] 经函数返回的引用 —— 不延长\n";
    { const Tracer& r = identity(Tracer("T2")); std::cout << "    in scope (r 已悬垂)\n"; }
    std::cout << "  [3] 普通临时，无人绑定\n";
    { Tracer("T3"); std::cout << "    after statement\n"; }
    std::cout << "  end\n";
}
```

实测输出：

```text
  [1] 直接绑定临时
    construct T1
    in scope
    destruct   T1        ← 延长生效：出了作用域才析构
  [2] 经函数返回的引用 —— 不延长
    construct T2
    destruct   T2        ← 语句结束就析构，"in scope" 打印时 r 已悬垂
    in scope (r 已悬垂)
  [3] 普通临时，无人绑定
    construct T3
    destruct   T3        ← 丢弃表达式：分号处立即析构
    after statement
  end
```

### 7. 完美转发

完整程序与实测输出见 §7 末尾。`good`（`T&&` + `std::forward<T>`）把左值传成左值、右值传成右值；`bad`（按值）的形参 `x` 是左值，`use` 永远命中 `const&` 版本——右值"偷不动"了，且 `std::string` 白白多一次拷贝构造。

---

## 参考与延伸阅读

- **cppreference — Value categories**：<https://en.cppreference.com/w/cpp/language/value_category>（lvalue/prvalue/xvalue 的权威定义）
- **cppreference — Reference initialization**：<https://en.cppreference.com/w/cpp/language/reference_initialization>（绑定规则与生命周期延长的条款出处）
- **cppreference — `std::move` / `std::forward`**：<https://en.cppreference.com/w/cpp/utility/move>
- **cppreference — Range-based for**：<https://en.cppreference.com/w/cpp/language/range-for>（`__range` 展开与 C++23 前后的临时对象生命周期）
- **P2718R0 — Wording for lifetime extension in range-based for loops**：<https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p2718r0.html>（§8.2 的规则出处）
- **Meyers, *Effective Modern C++*, Item 23–30**（移动语义、完美转发、"universal reference" 的经典论述）
- 本系列相关：[C++ 模板完全指南](Cpp模板完全指南.md)（`T&&` 推导与完美转发的模板语境）、[`std::bind` / lambda / `std::ref` 完全指南](std-bind-lambda-ref完全指南.md)（移动捕获 `[p = std::move(p)]`）、[事件系统设计](../advanced/01-事件系统设计.md)（`dispatch(const F&)` 与 sink setter 的实战现场）

---

> 回到 [教程主页](../README.md)，或继续 [C++ 模板完全指南](Cpp模板完全指南.md)（`T&&` 推导、SFINAE 与完美转发的下一层）。
