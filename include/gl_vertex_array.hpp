#pragma once

// ============================================================================
// VertexArray —— 顶点数组对象(VAO)封装
//
// 先建立心智模型:VAO 到底存了什么?它不是"对象容器",而是一张状态表:
//   1. 每个属性槽的格式(size / type / normalized / stride / offset)
//   2. 属性槽的数据从哪个 buffer 取 —— 调用 glVertexAttribPointer 的那一刻,
//      绑定在 GL_ARRAY_BUFFER 上的 buffer 被记进这张表(★ 最容易误解的点)
//   3. 哪些槽被启用(glEnableVertexAttribArray)
//   4. 每个槽的实例除数(glVertexAttribDivisor)
//   5. GL_ELEMENT_ARRAY_BUFFER 的绑定(EBO 也记在 VAO 名下!)
// 这就是为什么 draw 之前只要 bind VAO,不用重新配任何属性。
//
// ⚠ 生命周期警告(由下一步 Mesh 解决):
//   删除 VAO 引用着的 buffer,VAO 表里会留下悬空引用 → 下次 draw 崩溃;
//   所以 VAO + VBO + EBO 必须同生共死,析构顺序 VAO 最先。
// ============================================================================

#include "gl_object.hpp"

#include <glad/glad.h>

#include <cstdint>  // intptr_t

namespace tutorial::gl {

class VertexArray final : public GLObject<VertexArray> {
public:
    // 继承基类构造:无效占位 / NoCreate
    using GLObject<VertexArray>::GLObject;

    // 三件套之一:领名字(需要当前线程已有 GL 上下文)
    VertexArray() { glGenVertexArrays(1, &id_); }

    // 三件套之二:删除(基类 reset()/析构会调用)
    static void destroy(GLuint id) { glDeleteVertexArrays(1, &id); }

    // ---- 三件套之三:业务方法 ---------------------------------------------

    // bind 是 const:它改的是 GL 全局状态机,不是本对象自身的数据成员。
    // 这是 GL 封装特有的"const 不代表无副作用"场景,注释写清楚。
    void bind() const { glBindVertexArray(id_); }

    // static:解绑永远是"绑 0",跟具体哪个实例无关 —— 如实表达。
    static void unbind() { glBindVertexArray(0); }

    // ---- 属性配置(核心)---------------------------------------------------
    //
    // attribute() = 启用槽位 + 往格式表写一行。
    //
    // 两个刻意的封装决策(对比教程裸写法):
    //   ① 开头先 bind 自己:glVertexAttribPointer 作用在"当前绑定的 VAO"上,
    //      裸写法下如果忘了先 bind 本 VAO,属性会写进别的 VAO(经典 bug,
    //      而且不崩、只是画错,极难查)。封装的价值 = 让这个 bug 无从发生。
    //   ② offset 用"字节偏移"整数而不是 const void*:内部代做转换,
    //      消灭 (void*)(3 * sizeof(float)) 这类魔法数。
    //
    // 数据来源:调用此刻绑定在 GL_ARRAY_BUFFER 上的 buffer —— 所以典型顺序:
    //   vao.bind();  vbo.bind(GL_ARRAY_BUFFER);  vao.attribute(...);
    void attribute(GLuint index, GLint size, GLenum type, GLboolean normalized,
                   GLsizei stride, GLintptr offset) {
        bind();
        glEnableVertexAttribArray(index);
        glVertexAttribPointer(index, size, type, normalized, stride,
            reinterpret_cast<const GLvoid*>(static_cast<std::intptr_t>(offset)));
    }

    // 常用重载:float 属性(位置/uv/法线大多数场景就是它)
    void attribute(GLuint index, GLint size, GLsizei stride, GLintptr offset) {
        attribute(index, size, GL_FLOAT, GL_FALSE, stride, offset);
    }

    // 实例除数:index 槽的属性每 divisor 个实例前进一次。
    //   0 = 每顶点(默认);1 = 每实例(实例化渲染,教程 10.x 章会用)
    //   主动 bind 自己的理由同 attribute()。
    void attributeDivisor(GLuint index, GLuint divisor) {
        bind();
        glVertexAttribDivisor(index, divisor);
    }
};

} // namespace tutorial::gl
