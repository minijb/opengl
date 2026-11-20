#pragma once

#include "buffer.hpp"

#include <glad/glad.h>

#include <cstddef>
#include <span>
#include <utility>

namespace tutorial {

// VAO + VBO + EBO 的整体资源单元，同生共死。
// 不拆成三个独立 RAII 对象的原因：删除 buffer 时 GL 只从"当前 context 的
// 绑定点"解绑；VBO/EBO 在自己的 VAO 未绑定时先死，VAO 里会留下悬空引用。
//
// 析构要求 current context：Mesh 必须声明在 Window 之后（栈反向析构保证
// mesh 先于窗口销毁）。
class Mesh {
public:
    struct Attribute {
        GLuint index;  // shader 中的 location
        GLint size;    // 分量数，如 vec3 -> 3
        GLenum type;   // GL_FLOAT 等
        size_t offset; // 在单个顶点内的字节偏移
    };

    // indices == nullptr 时 draw() 走 glDrawArrays，否则走 glDrawElements(GL_UNSIGNED_INT)
    Mesh(const void* vertexData, size_t vertexByteSize, GLsizei vertexCount,
         std::span<const Attribute> attributes, GLsizei stride,
         const GLuint* indices = nullptr, size_t indexCount = 0) {
        glGenVertexArrays(1, &vao_);
        glBindVertexArray(vao_);

        // EBO 的 upload 必须发生在 VAO 绑定期间：VAO 会记录这条
        // GL_ELEMENT_ARRAY_BUFFER 绑定，之后 draw 只需绑定 VAO。
        vbo_.upload(GL_ARRAY_BUFFER, vertexData, vertexByteSize);
        if (indices != nullptr) {
            ebo_.upload(GL_ELEMENT_ARRAY_BUFFER, indices, indexCount * sizeof(GLuint));
            indexCount_ = indexCount;
        } else {
            vertexCount_ = vertexCount;
        }

        for (const Attribute& attribute : attributes) {
            const void* offset = reinterpret_cast<const void*>(attribute.offset);
            glVertexAttribPointer(attribute.index, attribute.size, attribute.type,
                                  GL_FALSE, stride, offset);
            glEnableVertexAttribArray(attribute.index);
        }
        glBindVertexArray(0);
    }

    ~Mesh() { glDeleteVertexArrays(1, &vao_); } // 名字 0 被静默忽略

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    // vbo_/ebo_ 是带移动语义的成员，默认移动即可正确转移；
    // 只有裸句柄 vao_ 和计数需要手动 exchange。
    Mesh(Mesh&& other) noexcept
        : vbo_(std::move(other.vbo_)),
          ebo_(std::move(other.ebo_)),
          vao_(std::exchange(other.vao_, 0)),
          vertexCount_(std::exchange(other.vertexCount_, 0)),
          indexCount_(std::exchange(other.indexCount_, 0)) {}

    Mesh& operator=(Mesh&& other) noexcept {
        if (this != &other) {
            glDeleteVertexArrays(1, &vao_); // 释放自己已有的，避免泄漏
            vbo_ = std::move(other.vbo_);
            ebo_ = std::move(other.ebo_);
            vao_ = std::exchange(other.vao_, 0);
            vertexCount_ = std::exchange(other.vertexCount_, 0);
            indexCount_ = std::exchange(other.indexCount_, 0);
        }
        return *this;
    }

    void draw() const {
        glBindVertexArray(vao_);
        if (indexCount_ > 0) {
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount_),
                           GL_UNSIGNED_INT, nullptr);
        } else {
            glDrawArrays(GL_TRIANGLES, 0, vertexCount_);
        }
    }

    GLuint get() const { return vao_; }

private:
    Buffer vbo_;
    Buffer ebo_;
    GLuint vao_ = 0;
    GLsizei vertexCount_ = 0;
    size_t indexCount_ = 0;
};

} // namespace tutorial
