#pragma once

#include <glad/glad.h>

#include <cstddef>
#include <utility>

namespace tutorial {

// VBO / EBO 通用的 RAII 封装。
// 规则（来自调研结论）：
//   1. 禁拷贝：GLuint 句柄是"伪装成整数的指针"，隐式拷贝 = double delete；
//      GL 对象深拷贝又"昂贵且几乎不可能"（OpenGL 官方文档）。
//   2. 破坏性移动：moved-from 置 0，析构时 glDeleteBuffers(0) 被静默忽略，天然安全。
//   3. 只作为 Mesh 的成员使用，不要让它以独立生命周期漂在 Mesh 之外——
//      VAO 会记录 EBO 绑定，VBO/EBO 先死会留下悬空引用（GL 4.5 spec §5.1.2）。
//   4. 析构要求 current context：声明顺序上必须位于 Window 之后。
class Buffer {
public:
    Buffer() { glGenBuffers(1, &id_); }
    ~Buffer() { glDeleteBuffers(1, &id_); }

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    Buffer(Buffer&& other) noexcept : id_(std::exchange(other.id_, 0)) {}
    Buffer& operator=(Buffer&& other) noexcept {
        if (this != &other) {
            glDeleteBuffers(1, &id_); // 释放自己已有的，避免移动赋值时泄漏
            id_ = std::exchange(other.id_, 0);
        }
        return *this;
    }

    void bind(GLenum target) const { glBindBuffer(target, id_); }

    void upload(GLenum target, const void* data, size_t byteSize,
                GLenum usage = GL_STATIC_DRAW) {
        bind(target);
        glBufferData(target, static_cast<GLsizeiptr>(byteSize), data, usage);
    }

    GLuint get() const { return id_; }

private:
    GLuint id_ = 0;
};

} // namespace tutorial
