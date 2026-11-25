#pragma once

// ============================================================================
// Buffer —— 第一个具体子类(VBO / EBO / UBO 的底层都是它)
//
// 这一步先保持最小:只演示"派生类要写的三样东西"。
// 注意:glGenBuffers 只是"领名字"(还没占显存),真正分配在 upload 里。
//
// 警告(留给后面 Mesh 一步解决的坑):
//   glDeleteBuffers 不会解除 VAO 对 buffer 的引用,单独删 buffer 会留悬空引用。
// ============================================================================

#include "gl_object.hpp"

#include <glad/glad.h>

namespace tutorial::gl {

class Buffer final : public GLObject<Buffer> {
public:
    // 继承基类构造:Buffer b;(无效占位)、Buffer b{NoCreate};
    // (同名签名被本类自己的 Buffer() 覆盖,不冲突)
    using GLObject<Buffer>::GLObject;

    // 三件套之一:分配(需要当前线程已有 GL 上下文)
    Buffer() { glGenBuffers(1, &id_); }

    // 三件套之二:删除(基类 reset()/析构会调用)
    static void destroy(GLuint id) { glDeleteBuffers(1, &id); }

    // ---- 三件套之三:业务方法(GL 3.3 经典 bind 流)----------------------

    // target: GL_ARRAY_BUFFER(VBO) / GL_ELEMENT_ARRAY_BUFFER(EBO) /
    //         GL_UNIFORM_BUFFER(UBO) / ...
    void bind(GLenum target) const { glBindBuffer(target, id_); }

    // static:解绑永远是"绑 0",跟具体哪个实例无关 —— 如实表达。
    static void unbind(GLenum target) { glBindBuffer(target, 0); }

    // 整块分配 + 上传。data = nullptr 表示只分配不填(之后 uploadSub 填)。
    // usage: GL_STATIC_DRAW(一次上传多次读)/ GL_DYNAMIC_DRAW(每帧改)。
    void upload(GLenum target, const void* data, GLsizeiptr size, GLenum usage) const {
        glBufferData(target, size, data, usage);
    }

    // 局部更新(必须先 upload 分配过;适合每帧只改一小块)。
    void uploadSub(GLenum target, GLintptr offset, const void* data, GLsizeiptr size) const {
        glBufferSubData(target, offset, size, data);
    }
};

} // namespace tutorial::gl
