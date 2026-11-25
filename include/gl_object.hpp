#pragma once

// ============================================================================
// GLObject<CRTP> —— 所有 OpenGL 对象的"唯一所有权"骨架(Magnum 风格)
//
// 本文件是 RAII 封装的第一块积木,只做一件事:
//   把"GL 句柄的生命周期"翻译成 C++ 的 move-only 语义。
//
// 核心思想(一句话):
//   GL 对象 = 唯一所有权资源,像 std::unique_ptr 一样:
//   拷贝编译报错、移动转移所有权、析构自动删除、moved-from 析构安全。
//
// 派生类只需提供三样东西:
//   1. 构造函数:调 glGenXXX / glCreateXXX 填充 id_
//   2. static void destroy(GLuint):调对应的 glDeleteXXX
//   3. 业务方法(bind / upload / ...)
//
// 为什么用 CRTP 而不是虚函数:
//   每种对象的删除函数不同(glDeleteBuffers / glDeleteVertexArrays / ...),
//   通过 Derived::destroy(id) 在编译期静态分派 —— 没有虚表、没有运行时开销。
// ============================================================================

#include <glad/glad.h>

#include <utility>  // std::exchange

namespace tutorial::gl {

// ─── "空对象"标签(Magnum 风格)───────────────────────────────────────────
// 用法:Buffer b{NoCreate};  —— 显式地"先占位,之后再赋值"。
// 与默认构造 Buffer b; 语义相同,但读代码的人一眼看出"这里故意不初始化"。
struct NoCreateT {
    explicit NoCreateT() = default;  // explicit: 防止 NoCreate 从 {} 被隐式推导出来
};
inline constexpr NoCreateT NoCreate{};  // constexpr 常量,头文件安全(无 ODR 问题)

// ─── CRTP 基类 ───────────────────────────────────────────────────────────
template<typename Derived>
class GLObject {
public:
    // 默认构造:无效占位(id_ = 0)。
    GLObject() = default;

    // 显式占位:同上,但意图明确(容器里、延迟初始化场景)。
    explicit GLObject(NoCreateT) {}

    // ---- 五法则(Rule of Five)-------------------------------------------

    // 1. 析构:句柄非 0 才删除。moved-from 对象(id_ 已被换走)析构 = 安全空操作。
    //    这一行是 GL RAII 封装里最容易写错、也最致命的地方。
    ~GLObject() { reset(); }

    // 2/3. 禁止拷贝:GL 的"名字"是全局唯一的,拷贝会让两个对象析构时
    //      double-delete。Hazel 引擎早期版本没删拷贝构造,是它的著名隐患。
    GLObject(const GLObject&) = delete;
    GLObject& operator=(const GLObject&) = delete;

    // 4. 移动构造:转移所有权,把源退回"无效"。
    //    std::exchange(other.id_, 0):读走旧值的同时写回 0 —— 一行顶三行。
    //    noexcept 关键:std::vector 扩容时只有 noexcept 移动才不退化为拷贝
    //    (拷贝被删 = 编译错误),所以这里 noexcept 不只是优化,是正确性。
    GLObject(GLObject&& other) noexcept
        : id_{std::exchange(other.id_, 0)} {}

    // 5. 移动赋值:先释放自己手里的(reset),再接手对方的。
    //    this == &other 的自赋值检查是标准写法(虽然罕见,但 std::swap 会走到)。
    GLObject& operator=(GLObject&& other) noexcept {
        if (this != &other) {
            reset();
            id_ = std::exchange(other.id_, 0);
        }
        return *this;
    }

    // ---- 观察器 ──────────────────────────────────────────────────────────

    // 取裸句柄:用于和纯 GL 代码/教程示例交互(比如传给 glGetIntegerv)。
    [[nodiscard]] GLuint handle() const { return id_; }

    [[nodiscard]] bool valid() const { return id_ != 0; }

    // explicit:允许 if (tex) {...},但不允许 tex + 1 这种隐式整数转换。
    explicit operator bool() const { return valid(); }

    // ---- 资源转移(与裸 API 世界互通的两座桥)────────────────────────────

    // 收养一个外部裸句柄(教程代码/第三方库创建的):
    //   auto tex = Texture::wrap(rawIdFromSomewhere);
    // 从此 wrapper 拥有它,析构时会删除。要求 id 是有效对象名(0 无效)。
    // 静态成员函数可以访问同类私有成员,所以能直接写 d.id_。
    [[nodiscard]] static Derived wrap(GLuint id) {
        Derived d{NoCreate};
        d.id_ = id;
        return d;  // NRVO/移动返回
    }

    // 放弃所有权:把裸句柄还给调用者,自己退回无效(析构不再删除)。
    // [[nodiscard]]:忽略了返回值 = 白白泄漏一个 GL 对象。
    [[nodiscard]] GLuint release() {
        return std::exchange(id_, 0);
    }

    // ---- 主动销毁 ────────────────────────────────────────────────────────
    // 与析构等价,但可以提前显式调用;对无效对象是安全空操作。
    void reset() {
        if (id_ != 0) {
            Derived::destroy(id_);  // 编译期静态分派到子类的 glDeleteXXX
            id_ = 0;
        }
    }

protected:
    // 直接接管句柄的构造(仅供 wrap() 内部使用;protected 挡住业务代码绕过 wrap)。
    explicit GLObject(GLuint id) : id_{id} {}

    // OpenGL "名字"。GL 保证 0 永远不是有效对象名 → 天然的"无效"哨兵值,
    // 这也是整套 moved-from/reset 设计能够成立的前提。
    GLuint id_ = 0;
};

} // namespace tutorial::gl
