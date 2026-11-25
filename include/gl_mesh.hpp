#pragma once

// ============================================================================
// Mesh —— "可绘制物":VAO + VBO + (可选)EBO 的组合所有权
//
// 本文件解决调研报告里指出的生命周期第一大坑:
//   glDeleteBuffers 不会解除 VAO 对 buffer 的引用。只删 VBO、留着 VAO,
//   下次 draw 就 use-after-delete 崩溃。
// 解法:三者被同一个 Mesh 拥有,同生共死,谁也别想单独先走。
//
// 关键机制:C++ 成员析构顺序 = 声明顺序的【逆序】。
//   声明顺序: vbo_ → ebo_ → vao_
//   析构顺序: vao_(先!)→ ebo_ → vbo_
//   VAO 先死 → 它对 vbo_/ebo_ 的引用随之蒸发 → 后两者再删除绝对安全。
//   "声明顺序"就是我们唯一要写对的东西。
//
// Mesh 不继承 GLObject:它不是"一个 GL 对象",而是几个对象的组合体
// (对应调研报告 §2.6 的 Magnum GL::Mesh 思路,但改为值语义直接持有)。
// 拷贝被成员连坐删除、移动由成员连坐合成 —— Mesh 一行五法则都不用写,
// 这是组合 RAII 的复利。
// ============================================================================

#include "gl_buffer.hpp"
#include "gl_vertex_array.hpp"

#include <glad/glad.h>

#include <cassert>
#include <initializer_list>

namespace tutorial::gl {

class Mesh {
public:
    // 一条顶点属性的描述(交错布局中的一列)。
    // 目前只支持 float 属性;带 type/normalized 的完整版留给 BufferLayout 一步。
    struct Attribute {
        GLuint index;     // 属性槽位 = shader 里 layout(location = N)
        GLint size;       // 分量数 1..4(pos3 → 3,uv2 → 2)
        GLsizei stride;   // 一个顶点总字节数(交错布局中所有属性共享同一个值)
        GLintptr offset;  // 本属性在顶点内的字节偏移
    };

    // 非索引网格:glDrawArrays 画。
    Mesh(const void* vertices, GLsizeiptr vertexBytes,
         std::initializer_list<Attribute> attributes)
        : Mesh{vertices, vertexBytes, nullptr, 0, GL_UNSIGNED_INT, attributes} {}

    // 索引网格:glDrawElements 画(indices 内的整数指向顶点下标)。
    // indexType: GL_UNSIGNED_BYTE / SHORT / INT(顶点数 > 65535 才需要 INT)。
    // 契约:构造完成即"可直接 draw",不存在半配置状态。
    Mesh(const void* vertices, GLsizeiptr vertexBytes,
         const void* indices, GLsizeiptr indexBytes, GLenum indexType,
         std::initializer_list<Attribute> attributes)
        : vbo_{}
        , ebo_{}
        , vao_{} {
        assert(attributes.size() > 0 && "Mesh 至少需要一条属性");
        assert((indices != nullptr || indexBytes == 0) && "给了 indexBytes 就必须给 indices");
        const GLsizei stride = attributes.begin()->stride;
        assert(stride > 0 && "stride 必须为正");

        vertexCount_ = static_cast<GLsizei>(vertexBytes / stride);

        // ---- 经典 GL 3.3 构造序列(顺序就是知识点)----
        vao_.bind();                                  // ① VAO 上台
        vbo_.bind(GL_ARRAY_BUFFER);                   // ② 数据源绑定(将被记入 VAO 的表)
        vbo_.upload(GL_ARRAY_BUFFER, vertices, vertexBytes, GL_STATIC_DRAW);
        for (const Attribute& a : attributes) {       // ③ 逐条写属性表(内部会再 bind,无害)
            vao_.attribute(a.index, a.size, a.stride, a.offset);
        }
        if (indexBytes > 0) {
            // ④ EBO 的绑定属于 VAO 状态 —— 必须在 VAO 绑定期间做!
            //    (教程里 Hazel 特意注释过:没有绑定 VAO 时绑 ELEMENT 是无效操作)
            ebo_.bind(GL_ELEMENT_ARRAY_BUFFER);
            ebo_.upload(GL_ELEMENT_ARRAY_BUFFER, indices, indexBytes, GL_STATIC_DRAW);
            indexType_ = indexType;
            indexCount_ = static_cast<GLsizei>(indexBytes / indexSize(indexType));
        }
        VertexArray::unbind();                        // ⑤ 收工:状态全部封存进 VAO

        // 注意这里不解绑 VBO/GL_ARRAY_BUFFER:GL_ARRAY_BUFFER 的绑定不是 VAO 状态
        // (解不解对正确性无影响,留着只是全局状态卫生问题)
    }

    // ---- 绘制 --------------------------------------------------------------
    // 不在结尾 unbind VAO:下一次 draw 自会换绑,省一次驱动调用(高性能习惯)。
    void draw(GLenum mode = GL_TRIANGLES) const {
        vao_.bind();
        if (indexCount_ > 0) {
            glDrawElements(mode, indexCount_, indexType_, nullptr);
        } else {
            glDrawArrays(mode, 0, vertexCount_);
        }
    }

    [[nodiscard]] GLsizei vertexCount() const { return vertexCount_; }
    [[nodiscard]] GLsizei indexCount() const { return indexCount_; }

private:
    // ↑ 成员声明顺序就是本类的核心设计:
    //   析构逆序 = vao_ 先死(解除引用)→ ebo_ → vbo_,悬空引用结构性不可能。
    Buffer vbo_;
    Buffer ebo_;
    VertexArray vao_;

    GLsizei vertexCount_ = 0;  // 非索引 draw 用
    GLsizei indexCount_ = 0;   // >0 表示索引网格
    GLenum indexType_ = GL_UNSIGNED_INT;

    static GLsizeiptr indexSize(GLenum indexType) {
        switch (indexType) {
        case GL_UNSIGNED_BYTE:  return 1;
        case GL_UNSIGNED_SHORT: return 2;
        case GL_UNSIGNED_INT:   return 4;
        default:                return 0;  // assert 会兜底
        }
    }
};

} // namespace tutorial::gl
