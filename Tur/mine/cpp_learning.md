## lambda

基础构成

```cpp
[ 捕获列表 ] ( 参数列表 ) mutable -> 返回类型 { 函数体 }
   ↑              ↑          ↑          ↑            ↑
   │              │          │          │            └─ 要执行的代码
   │              │          │          └─ 尾随返回类型（多数情况可省，编译器推导）
   │              │          └─ 允许修改按值捕获的副本（默认 operator() 是 const）
   │              └─ 和普通函数一样的形参
   └─ 告诉编译器："我要把外面哪些变量带进来"
```




| 写法        | 含义                       | 内部存储形式                |
| --------- | ------------------------ | --------------------- |
| `[]`      | 不捕获                      | 无成员变量；可退化为函数指针        |
| `[x]`     | 按值捕获 `x`（副本）             | 一个 `T x;` 成员          |
| `[&x]`    | 按引用捕获 `x`                | 一个 `T* x;` 成员（指向 `x`） |
| `[=]`     | 按值捕获**所有用到的**外层变量        | 多个值成员                 |
| `[&]`     | 按引用捕获**所有用到的**外层变量       | 多个指针成员                |
| `[=, &x]` | 默认按值，但 `x` 按引用           | 混合                    |
| `[&, x]`  | 默认按引用，但 `x` 按值           | 混合                    |
| `[this]`  | 捕获当前对象的 `this` 指针（成员函数内） | `C* this;`            |


注意： 

- 捕获时机 --- 定义的时候就被捕获了(不是在使用的时候)。
- 值捕获默认是 const 的， 想要修改可以使用 mutable
- 本质是一个类

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



## std::bind 参数预填充



```cpp
#include <functional>
using namespace std::placeholders;   // 引入 _1, _2, ...

int add(int a, int b, int c) { return a + b + c; }

// 预填前两个参数，留下一个"位置占位符" _1
auto add5and3 = std::bind(add, 5, 3, _1);

std::cout << add5and3(10);   // 18  (= 5 + 3 + 10)
```



最常用方法 ： 因为成员函数有一个隐含的 this 指针， 此时就可以使用bind将他们绑定在一起

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

等价的 lamdba

```cpp
auto fn = [&calc](int a, int b) { return calc.add(a, b); };
```

其返回值只能使用 auto 进行捕获 或者塞到 `std::function` 中

```cpp
std::function<int(int)> fn = std::bind(add, _1, _1, _1);
```

**std::bind** 的参数默认使用值拷贝， 但有的使用希望使用 引用类型。 此时可以使用 `std::ref`

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

### 2.6 为什么现代 C++ 几乎不用 `std::bind`

Scott Meyers 在《Effective Modern C++》Item 34 标题就是 **"Prefer lambdas to**++ `std::bind`++**"**；Herb Sutter 和 C++ Core Guidelines 同样建议避开它。理由：


| 问题                | 说明                                                                                      |
| ----------------- | --------------------------------------------------------------------------------------- |
| **错误信息难读**        | `std::bind` 的返回类型由标准库内部嵌套模板拼成，一行报错动辄几十屏。                                                |
| **无法内联**          | 中间隔着一层 `std::_Bind` 包装，编译器很难跨过去优化。lambda 直接是一个类，能被完全内联。                                 |
| **重载函数无法直接 bind** | `std::bind(f, ...)` 里如果 `f` 有重载，编译器不知道绑哪个，你得手写一个函数指针转型。lambda 没这问题。                     |
| **参数传递语义晦涩**      | 按值？按引用？需不需要 `std::ref`？读代码的人要记住一堆规则。lambda 的 `[=]` / `[&]` 一眼看清。                        |
| **move 语义处理差**    | C++11 时代的接口设计，难以高效转发只移动类型（`std::unique_ptr` 等）。lambda 的初始化捕获 `[p = std::move(p)]` 干净利落。 |


## `std::ref` / `std::cref`：把"引用"塞进按值收参的接口

- `std::bind` / `std::bind_front` 复制传入的可调用对象和实参；
- `std::thread` / `std::async` 按值复制线程函数的参数（**即使你写** `void f(int&)`**，线程里也是副本**）；
- `std::make_tuple` / `std::make_pair` 按值复制每个元素；
- 容器 `std::vector<T>` 里 `T` 不能是引用（`vector<int&>` 编译不过）。

这些接口的设计动机是**值语义的安全性**（没有悬垂引用风险）。但当你**确实需要传引用**时怎么办？

`std::ref` / `std::cref` 就是答案：它返回一个 **"引用代理"对象** ，这个对象本身是可拷贝的值类型，但**行为像引用**。

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

特征


| 性质                                       | 含义                                         |
| ---------------------------------------- | ------------------------------------------ |
| **可拷贝、可赋值**（C++17 起还是 TriviallyCopyable） | 能进容器、能按值传                                  |
| **隐式转换为** `T&`                           | 凡是收 `T&` 的函数都能直接吃它                         |
| **可调用**（若 `T` 可调用）                       | 能放进 `std::function`、能当函数对象                 |
| **不能包临时对象**                              | `std::ref(临时)` 的重载被 `= delete`，编译期拒绝——避免悬垂 |


