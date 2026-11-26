# 现代 C++ 惯用法调研报告:[[nodiscard]] 与 CRTP

> 调研方式:标准条款 + 主流编译器行为核对,全部示例代码在本机 Clang 18 `-std=c++20 -Wall -Wextra` 下编译验证(零警告、行为正确)。
> 面向读者:同 [graphics_api_data_structures.md](graphics_api_data_structures.md) —— 正在给自己项目的 GL 封装类(C++17/20)补充理论背景的学习者。
> 为什么是这两个主题:`include/gl_object.hpp` 已经同时用上了它们 —— `GLObject<Derived>` 是 CRTP,`handle()/valid()/wrap()/release()` 标了 `[[nodiscard]]`。本报告把"项目里为什么这么写"讲透。

## 0. 一页速览

| | `[[nodiscard]]` | CRTP |
|---|---|---|
| 一句话 | 返回值被丢弃时让编译器报警 | 类把自己作为模板参数传给基类 |
| 引入 | C++17(带原因/构造函数版为 C++20) | 惯用法,1995 年起流行 |
| 解决的问题 | "调用了但没看结果"型 bug | 基类需要在**编译期**知道派生类是谁 |
| 代价 | 无(纯属性) | 无虚表,但不可运行时多态 |
| 本项目用例 | `GLObject::release()` | `GLObject<Derived>` 资源骨架 |

两者合流的典型效果(`gl_object.hpp` 真实代码):

```cpp
GLuint raw = tex.release();   // 不接住返回值 = 白白泄漏一个 GL 对象 → 编译器会警告
auto tex  = Texture::wrap(id); // 基类方法能"构造并返回派生类" —— 普通基类做不到
```

---

## 1. `[[nodiscard]]`:盯住"被扔掉的返回值"

### 1.1 问题

一类高频 bug 的模式是:**函数的结果就是全部意义,调用者却把它扔了**。

```cpp
// 三个真实世界的"扔了就是 bug":
some_map.insert(kv);              // 返回值告诉你"其实已存在,没插入"
auto fut = std::async(work);      // 如果写成 std::async(work); 会阻塞等它跑完
close(fd);                        // 返回 -1 表示失败,失败 = fd 还在,泄漏
```

这类错误没有任何语法问题,编译器默认一声不吭。`[[nodiscard]]`(读作 "no discard",勿丢弃)是 C++17 引入的标准属性:**标在函数上之后,丢弃返回值的调用点会得到编译警告**。

### 1.2 基本用法

```cpp
[[nodiscard]] bool validate(const std::string& s);

validate(cfg);              // ⚠️ warning: ignoring return value
bool ok = validate(cfg);    // ✅
(void)validate(cfg);        // ✅ 显式转 void = "我有意丢弃",不警告
```

要点:

- 只是**警告**不是错误(除非 CI 里配合 `-Werror`);
- 只在"调用结果被整体丢弃"时触发;`if (validate(s))`、`auto x = f() + g()` 这类**使用**场景不触发;
- `(void)` 强转是标准认可的唯一规范抑制写法(C++23 起也可用 `std::ignore = f();`)。

### 1.3 三种标注位置

| 位置 | 标准版本 | 触发条件 |
|---|---|---|
| 函数声明 | C++17 | 丢弃**该函数**返回值 |
| class/enum/别名声明 | C++17 | 丢弃**任何返回该类型**的函数返回值 |
| 构造函数 | C++20(P1771R1) | `T();` 这种纯临时构造被丢弃 |

类型级标注是实践中最有价值的一种 —— 定义一个"结果类型",所有返回它的函数自动受保护:

```cpp
struct [[nodiscard]] error_info { int code; std::string msg; };

error_info make_error();
make_error();      // ⚠️ 警告,与函数本身是否标注无关
```

这正是 `Result<T>` / `std::expected` 风格错误处理的标准配套:错误结果"看了才算处理过"。

### 1.4 C++20:带原因

```cpp
[[nodiscard("泄漏资源:必须用 release() 或让它随 RAII 析构")]]
static GLuint acquire();
```

原因字符串直接出现在警告里,比裸警告名友好得多,适合放在团队规范里。

### 1.5 标准库用例(节选)

| 接口 | 为什么必须 nodiscard |
|---|---|
| `std::async(f, ...)` | 丢弃返回的 future → 析构时**阻塞等任务跑完**,并发瞬间变成串行 |
| `operator new(std::nothrow)` | 不接指针 = 白分配 |
| `std::launder(p)` | 返回值不用 = 等于没调用 |
| `std::allocate_shared/make_*` | 同上 |
| `std::bit_cast` | 纯函数,丢弃毫无意义 |

### 1.6 一个著名的反例:别滥用

`std::list::remove` 被标准标了 `[[nodiscard]]`,但它明明是**修改性操作**(删元素有副作用),返回值只是删了几个。社区长期吐槽:想"清空匹配项"的人被迫写 `(void)lst.remove(x);`。

教训:`[[nodiscard]]` 应标在"**丢弃返回值 = 调用本身失去意义**"的函数上;有实质副作用的函数即使返回统计信息,也慎标。对照本项目:`reset()` 有删除 GL 对象的副作用,所以**不**标;`release()` 的唯一意义就是把句柄交给你,所以**必须**标。

### 1.7 编译器支持

| 编译器 | 首次支持 | 警告名/编号 |
|---|---|---|
| GCC | 7 | `-Wunused-result` |
| Clang | 3.9 | `-Wunused-result` |
| MSVC | VS2017 15.3 | C4834 |

建议:库代码照标;CI 中可对项目自身代码开启对应 `-Werror`,第三方警告单独豁免。

---

## 2. CRTP:编译期多态

### 2.1 形式

CRTP(Curiously Recurring Template Pattern,奇异递归模板模式,James Coplien 1995 年命名)的"奇异递归"指:**派生类继承自以自己为模板参数的基类**。

```cpp
template <typename Derived>
class Base { /* 用 Derived 做事 */ };

class Derived : public Base<Derived> { };   // ← 把自己传进去
```

### 2.2 为什么非它不可

普通基类在**定义时不知道派生类是谁**。一旦把 `Derived` 作为模板参数传进来,基类就能:

1. `static_cast<Derived*>(this)` 精确下转 —— 调用派生类的成员;
2. 直接**构造、返回** `Derived` 对象(本报告 §2.6 的 `wrap()` 就是这个);
3. 以 `Derived` 为类型声明接口参数,静态成员天然按 `Derived` 一类一隔离(§2.5)。

这一切都发生在编译期:没有虚表、没有运行时开销、可以内联。

### 2.3 场景一:静态多态 / 非虚接口(NVI)

```cpp
template <typename Derived>
class Animal {
public:
    void speak() {                       // 统一入口
        static_cast<Derived*>(this)->do_speak();   // 编译期分派
    }
};
class Dog : public Animal<Dog> { public: void do_speak(); };
```

与虚函数的本质区别:

| | `virtual` | CRTP |
|---|---|---|
| 分派时机 | 运行期(查 vtable) | 编译期(直接调用,可内联) |
| `Animal&` 能否统一装 Dog/Cat | 能 | 不能 —— `Animal<Dog>` 和 `Animal<Cat>` 是**两个不相关的类型** |
| 适用 | 需要"运行时换实现"、跨 DLL 边界 | 骨架固定、实现点在编译期已知 |

选型口诀:**需要"用一个基类指针装下所有子类"就必须虚函数;如果每一步用什么类型编译期就确定了,CRTP 免费拿到多态的代码复用。**

### 2.4 场景二:Mixin,一个 `<` 换来全部比较

```cpp
template <typename Derived>
struct Comparable {
    bool operator>(const Derived& o) const { return o < static_cast<const Derived&>(*this); }
    bool operator<=(const Derived& o) const { return !(o < static_cast<const Derived&>(*this)); }
    bool operator>=(const Derived& o) const { return !(static_cast<const Derived&>(*this) < o); }
};

struct Version final : Comparable<Version> {
    int value = 0;
    bool operator<(const Version& o) const { return value < o.value; }
};
// Version 自动获得 >、<=、>=,且语义一定自洽
```

派生类只写最小的 `operator<`,基类靠"拿到自己完整派生类型"推导出其余操作符。(C++20 之后这类需求多数可直接写 `operator<=>` 一次搞定,Mixin 更适合聚合不相关的横切能力。)

### 2.5 场景三:静态成员按派生类隔离

```cpp
template <typename T> struct Counter {
    static inline int count = 0;   // 每个 Counter<X> 实例化各有一份
    Counter()  { ++count; }
    ~Counter() { --count; }
};
struct A : Counter<A> {};
struct B : Counter<B> {};          // Counter<A>::count 与 Counter<B>::count 互不影响
```

如果不用 CRTP(所有类继承同一个非模板基类),所有类共享一个计数器 —— 这是 CRTP"一类一份"特性的直接应用。

### 2.6 本项目实战:`gl_object.hpp` 逐块拆解

项目里所有 GL 封装(`Buffer` / `Shader` / `Program` / `VertexArray`)继承同一个骨架:

```cpp
template<typename Derived>
class GLObject { ... };                    // CRTP 基类

class Buffer final : public GLObject<Buffer> {          // 传入自己
    using GLObject<Buffer>::GLObject;                   // 继承构造函数
    Buffer() { glGenBuffers(1, &id_); }                 // 三件套①:分配
    static void destroy(GLuint id) { glDeleteBuffers(1, &id); }  // 三件套②:删除
    void bind(GLenum target) const { glBindBuffer(target, id_); } // 三件套③:业务
};
```

**为什么这里必须是 CRTP、虚函数不行:**

1. **删除函数各不相同** —— `glDeleteBuffers / glDeleteVertexArrays / glDeleteProgram / glDeleteShader`。基类的析构和 `reset()` 要调"正确的那个",通过 `Derived::destroy(id_)` 编译期静态分派,零开销;若用虚函数,每个对象多一个 vtable 指针不说,析构路径还要求基类有虚析构,纯属浪费 —— 这些类天生**多态不可用**(move-only、按值存储在容器里)。
2. **`wrap()` 要返回派生类**:

   ```cpp
   [[nodiscard]] static Derived wrap(GLuint id) {
       Derived d{NoCreate};
       d.id_ = id;        // static 成员函数可访问同类私有成员
       return d;
   }
   ```

   普通基类只能返回 `Base&`,根本造不出 `Texture`。CRTP 下调用侧拿到的是完整值类型:`auto tex = Texture::wrap(id);`,移动构造一路返回,零额外开销。
3. **Rule of Five 骨架只写一次**:禁拷贝(GL 名字全局唯一,拷贝 = double-delete)、`noexcept` 移动(`std::exchange` 换出 0 哨兵,moved-from 析构安全)、析构经 `reset()` 空操作安全。四个派生类零重复。

**派生类的义务只有"三件套"**:构造函数领名字、`static void destroy(GLuint)`、业务方法。忘了写会怎样?见下节。

### 2.7 给 CRTP 加安全带

- **C++17**:基类内 `static_assert` 检查派生类实现,错误早暴露:

  ```cpp
  static_assert(std::is_invocable_v<decltype(Derived::destroy), GLuint>,
                "Derived 必须提供 static void destroy(GLuint)");
  ```

  (放在基类成员函数体内,实例化时才检查 —— 基类定义处 `Derived` 还是残缺类型,这是 CRTP 的常规限制。)
- **C++20**:concepts 直接约束模板参数,报错信息指出"谁、缺什么":

  ```cpp
  template<typename D>
  concept GlDestroyable = requires(unsigned id) { D::destroy(id); };

  template<GlDestroyable Derived>
  class GLObject { ... };
  // 少写 destroy 的派生类:一条"constraints not satisfied"直指病灶
  ```

- **C++23 deducing this(P0847)**:显式对象形参 `void f(this auto&& self)` 让非模板基类的方法也能拿到派生类型,可以取代**大部分为 NVI 而生**的 CRTP。本项目这种"基类要构造/返回派生类对象"的用法仍以 CRTP 更直接;两者将长期共存。

### 2.8 优缺点对照

| 优点 | 缺点/限制 |
|---|---|
| 无虚表、可内联,编译期完成分派 | 无法运行时多态:`GLObject<Buffer>*` 装不下 `VertexArray` |
| 骨架/横切能力零重复(DRY) | 每个实例化产生独立代码,极端时模板膨胀 |
| 基类可精确下转、可构造派生类 | 编译报错冗长(可用 §2.7 的 concepts 缓解) |
| 一类一份静态成员 | 基类定义处 `Derived` 不完整,只能"声明式"使用它 |

---

## 3. 合流点:两个惯用法在 `gl_object.hpp` 里如何咬合

`gl_object.hpp` 是一个"CRTP 骨架 + nodiscard 契约"的标准样本,每个 `[[nodiscard]]` 都有明确理由:

| 成员 | 标注 | 理由(对照 §1.6 的判断标准) |
|---|---|---|
| `GLuint release()` | ✅ `[[nodiscard]]` | 唯一意义就是交出句柄;丢弃 = GL 对象泄漏 |
| `static Derived wrap(GLuint)` | ✅ | 丢弃 = 一个已收养的 RAII 对象没人管,析构立即删资源 |
| `GLuint handle()` / `bool valid()` | ✅ | 纯观察器,丢弃无意义(轻量,无副作用争议) |
| `void reset()` | ❌ 不标 | 有删除资源的**副作用**,对照 `std::list::remove` 的教训 |
| `explicit operator bool` | ❌ 不标 | 转换语义,`if (obj)` 已覆盖 |

三条可迁移的通用规则:

1. **副作用函数不标,纯结果函数必标**(§1.6);
2. **"结果类型"整类标注**,一次覆盖所有返回它的接口(§1.3);
3. **CRTP 基类是放公共契约的好位置** —— `release/wrap` 写一次,四种资源类全部继承同样的防泄漏检查。

---

## 4. 实践清单

给 `[[nodiscard]]`:
- [ ] 错误码/结果对象/期望值类返回 → 必标
- [ ] 纯计算、无副作用函数 → 标
- [ ] 返回"结果类型"的接口群 → 优先在类型上标一次
- [ ] 有副作用的函数 → 不标(哪怕也返回点统计信息)
- [ ] 有意丢弃处写 `(void)f();`,让"丢弃"在代码里可见

给 CRTP:
- [ ] 先问"需要运行时多态吗?"需要 → 虚函数;不需要 → CRTP
- [ ] 派生类契约用 concepts(C++20)或 `static_assert` 固化
- [ ] 基类提供 Rule of Five 骨架 + `using Base<Derived>::Base;` 继承构造
- [ ] move-only 资源类:移动必须 `noexcept`,`std::exchange` 换出哨兵值

---

## 5. 参考

- ISO C++ 标准条款 [dcl.attr.nodiscard];P1301R1(`[[nodiscard("reason")]]`);P1771R1(构造函数标注)
- James O. Coplien, *Curiously Recurring Template Patterns*, C++ Report, 1995(术语出处)
- P0847R7 *Deducing this*(C++23,对 CRTP/NVI 的影响)
- 本项目源码:`include/gl_object.hpp`(CRTP 骨架)、`include/gl_buffer.hpp` 等派生类(三件套)
- 关联文档:[graphics_api_data_structures.md](graphics_api_data_structures.md)(§1.2 的 Magnum RAII 对比即本骨架的设计来源)
