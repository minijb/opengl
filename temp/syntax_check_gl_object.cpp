// 编译期验证(不链接、不运行):gl_object.hpp / gl_buffer.hpp /
// gl_vertex_array.hpp 的类型契约。全部是 static_assert + 永不执行的
// 函数 —— 只证明"形状对"。
#include "gl_buffer.hpp"
#include "gl_mesh.hpp"
#include "gl_object.hpp"
#include "gl_program.hpp"
#include "gl_vertex_array.hpp"

#include <type_traits>
#include <utility>

using tutorial::gl::Buffer;
using tutorial::gl::VertexArray;

// ---- 五法则契约(两种类型都过一遍)----
static_assert(std::is_nothrow_move_constructible_v<Buffer>, "移动构造必须 noexcept");
static_assert(std::is_nothrow_move_assignable_v<Buffer>, "移动赋值必须 noexcept");
static_assert(!std::is_copy_constructible_v<Buffer>, "必须禁止拷贝");
static_assert(!std::is_copy_assignable_v<Buffer>, "必须禁止拷贝赋值");
static_assert(std::is_default_constructible_v<Buffer>, "需要无效占位构造");
static_assert(std::is_constructible_v<Buffer, tutorial::gl::NoCreateT>, "需要 NoCreate 构造");
static_assert(!std::is_convertible_v<GLuint, Buffer>, "不允许 GLuint 隐式转换成 Buffer");

static_assert(std::is_nothrow_move_constructible_v<VertexArray>);
static_assert(std::is_nothrow_move_assignable_v<VertexArray>);
static_assert(!std::is_copy_constructible_v<VertexArray>);
static_assert(!std::is_copy_assignable_v<VertexArray>);
static_assert(std::is_nothrow_swappable_v<VertexArray>, "swap 必须 noexcept");

// noexcept 移动的直接受益者:swap 也 noexcept(内部用移动实现)
static_assert(std::is_nothrow_swappable_v<Buffer>, "swap 必须 noexcept");

// ---- Mesh 契约:组合所有权的“连坐”效果 ----
static_assert(std::is_nothrow_move_constructible_v<tutorial::gl::Mesh>);
static_assert(std::is_nothrow_move_assignable_v<tutorial::gl::Mesh>);
static_assert(!std::is_copy_constructible_v<tutorial::gl::Mesh>, "成员不可拷贝 → 整体不可拷贝");
static_assert(!std::is_default_constructible_v<tutorial::gl::Mesh>, "构造即完备,不留空壳");
static_assert(std::is_nothrow_swappable_v<tutorial::gl::Mesh>);

// ---- Program 契约 ----
// ★ 实战教训:这里【故意不】断言 nothrow ——
//   成员 unordered_map 的移动构造是否 noexcept 是实现相关的:
//   libstdc++ 是(g++ 通过),MSVC STL 目前不是(clang+MSVC STL 失败),
//   标准只保证“可移动不抛”的容器语义上不抛,但没标 noexcept。
//   往 RAII 类里加标准容器成员,noexcept 契约就会打折 —— 这是真实的
//   跨平台代价,记入笔记。(若必须要 nothrow:换缓存结构/手写移动)
static_assert(std::is_move_constructible_v<tutorial::gl::Program>);
static_assert(std::is_move_assignable_v<tutorial::gl::Program>);
static_assert(!std::is_copy_constructible_v<tutorial::gl::Program>);
static_assert(!std::is_convertible_v<GLuint, tutorial::gl::Program>);
// 缓存随对象一起移动(默认移动语义接管 map):移动后的对象仍能用旧缓存
static_assert(std::is_move_constructible_v<std::unordered_map<std::string, GLint>>);

// ---- API 形状编译检查(函数永不被调用,所以 wrap/release 不会真的泄漏)----
namespace {

void bufferApiShape(GLuint raw) {
    Buffer a;                          // 无效占位
    Buffer b{tutorial::gl::NoCreate};  // 显式占位
    (void)b;

    a = Buffer{};                      // 移动赋值(临时对象 → a)
    Buffer c = Buffer::wrap(raw);      // 收养裸句柄
    (void)c.release();                 // 放弃所有权(c 变无效,即使真跑也不泄漏)

    Buffer d{std::move(a)};            // 移动构造(此后 a 无效,a 析构是空操作)
    d.bind(GL_ARRAY_BUFFER);
    Buffer::unbind(GL_ARRAY_BUFFER);   // static:解绑不需要实例
    d.upload(GL_ARRAY_BUFFER, nullptr, 64, GL_STATIC_DRAW);
    d.uploadSub(GL_ARRAY_BUFFER, 0, nullptr, 64);
    (void)d.handle();
    (void)d.valid();
    if (d) { /* explicit operator bool 只允许在条件里 */ }
    d.reset();

    std::swap(c, d);                   // swap 用移动实现,无拷贝
}

void vaoApiShape() {
    VertexArray vao;                   // 领名字
    Buffer vbo;
    vao.bind();                        // 先绑 VAO
    vbo.bind(GL_ARRAY_BUFFER);         // 再绑数据源(被记入 VAO 的表)
    vao.attribute(0, 3, 20, 0);        // float 重载:pos, stride=20, offset=0
    vao.attribute(1, 2, 20, 12);       // uv:offset=12 字节(3 个 float 之后)
    vao.attribute(1, 2, GL_FLOAT, GL_FALSE, 20, 12);  // 完整版,同上
    vao.attributeDivisor(1, 1);        // 实例化:每实例前进一次
    VertexArray::unbind();             // static 解绑

    VertexArray moved = std::move(vao);  // 此后 vao 无效,析构安全
    (void)moved;
    std::swap(vao, moved);             // swap 用移动实现
}

void meshApiShape(const float* vertices) {
    using tutorial::gl::Mesh;

    // 非索引:本仓库 cubeVertices 的形状(pos3 + uv2,交错,stride 20)
    Mesh cube{vertices, 36 * 5 * sizeof(float),
        {{0, 3, 20, 0}, {1, 2, 20, 12}}};
    cube.draw();                    // 默认 GL_TRIANGLES
    cube.draw(GL_TRIANGLE_STRIP);
    (void)cube.vertexCount();

    // 索引版
    const unsigned int indices[] = {0, 1, 2};
    Mesh indexed{vertices, 36 * 5 * sizeof(float),
        indices, sizeof(indices), GL_UNSIGNED_INT,
        {{0, 3, 20, 0}}};
    indexed.draw();
    (void)indexed.indexCount();

    Mesh moved = std::move(cube);   // 整体移动:三个成员一起转移
    (void)moved;
    // Mesh copy = cube;            // ← 编译错误:成员连坐删除拷贝
    std::swap(moved, indexed);
}

void programApiShape(const glm::mat4& m, const glm::vec3& v3, const glm::vec2& v2) {
    using tutorial::gl::Program;

    Program program{R"glsl(#version 330 core
        void main() {})glsl",
                    R"glsl(#version 330 core
        void main() {})glsl"};
    if (!program) { /* 构造失败 = 无效对象(真实运行时才会发生) */ }

    program.use();
    program.unuse();
    program.setMat4("uModel", m);        // setter 内部自动 use()
    program.setMat3("uModel", glm::mat3{m});
    program.setVec3("uColor", v3);
    program.setVec2("uOffset", v2);
    program.setInt("uTexture", 0);
    program.setFloat("uMix", 0.5F);
    (void)program.uniform("uModel");    // 首次查询驱动,之后缓存命中
    (void)program.uniform("uModel");    // 第二次:O(1)

    Program p2 = Program::fromFiles("a.vs", "b.fs");
    (void)p2;
    Program moved = std::move(program);   // 缓存跟着一起走
    (void)moved;
    std::swap(moved, p2);
}

} // namespace
