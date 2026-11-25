#pragma once

// ============================================================================
// Program —— 着色器程序:编译 → 链接 → uniform 缓存,一步到位
//
// 两个对象的分工(Magnum/GL 界共识):
//   Shader(编译产物)= 一次性零件:编译、attach、链接完成后就删,
//                      仓库里只留最终装配好的 Program;
//   Program(链接产物)= 长期资产:uniform 表、状态都挂在它名下。
//
// 错误处理风格(与本项目现有惯例一致):
//   构造失败【不抛异常】,得到"无效对象"(错误日志已打到 stderr),
//   用 if (program) 检查 —— 对应现有 createProgram() 返回 0 的模式。
//   (另一种流派是抛异常携带 info log;届时局部 Shader 的 RAII 析构
//    会自动清理,这正是 RAII 的卖点。本项目选了更朴素的返回码风格。)
//
// 本文件消灭的两个每帧性能浪费(调研报告 §3):
//   ① glGetUniformLocation 的字符串哈希查找 —— 用 unordered_map 缓存;
//   ② 经典坑:setter 忘了先 use() 会把 uniform 写进别的 program ——
//      setter 开头自动 use(),顺序坑结构性消失(GL 3.3 没有 GL 4.1 的
//      glProgramUniform*,只能在经典路径上防御)。
// ============================================================================

#include "gl_object.hpp"

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>

namespace tutorial::gl {

// ─── Shader:一次性零件(只在 Program 构造过程内部出现)────────────────
class Shader final : public GLObject<Shader> {
public:
    using GLObject<Shader>::GLObject;

    // 编译一段源码。失败:打日志并保持无效(局部对象随后自动析构清理)。
    Shader(GLenum type, const char* source) {
        id_ = glCreateShader(type);
        glShaderSource(id_, 1, &source, nullptr);
        glCompileShader(id_);
        GLint ok = GL_FALSE;
        glGetShaderiv(id_, GL_COMPILE_STATUS, &ok);
        if (ok != GL_TRUE) {
            char log[4096]{};
            glGetShaderInfoLog(id_, sizeof(log), nullptr, log);
            std::fprintf(stderr, "Shader compilation failed (%s):\n%s\n",
                type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
            reset();
        }
    }

    static void destroy(GLuint id) { glDeleteShader(id); }

    void attachTo(GLuint program) const { glAttachShader(program, id_); }
    void detachFrom(GLuint program) const { glDetachShader(program, id_); }
};

// ─── Program:长期资产 ───────────────────────────────────────────────────
class Program final : public GLObject<Program> {
public:
    using GLObject<Program>::GLObject;

    // 从源码字符串编译 + 链接。失败得到无效对象(日志已打印)。
    // (几何/细分着色器等重载,等课程用到 4.9 章再按需加。)
    explicit Program(const char* vertexSource, const char* fragmentSource) {
        // 局部 RAII:编译失败/提前 return,两个 Shader 都自动清理
        Shader vertex{GL_VERTEX_SHADER, vertexSource};
        Shader fragment{GL_FRAGMENT_SHADER, fragmentSource};
        if (!vertex || !fragment) return;  // 日志已在 Shader 构造里打出

        id_ = glCreateProgram();
        vertex.attachTo(id_);
        fragment.attachTo(id_);
        glLinkProgram(id_);
        // 链接完立刻脱钩 + Shader 析构即释放:"零件装配完就扔"
        vertex.detachFrom(id_);
        fragment.detachFrom(id_);

        GLint ok = GL_FALSE;
        glGetProgramiv(id_, GL_LINK_STATUS, &ok);
        if (ok != GL_TRUE) {
            char log[4096]{};
            glGetProgramInfoLog(id_, sizeof(log), nullptr, log);
            std::fprintf(stderr, "Program linking failed:\n%s\n", log);
            reset();  // 删掉失败的 program,保持无效状态
        }
    }

    static void destroy(GLuint id) { glDeleteProgram(id); }

    // ---- 使用 ---------------------------------------------------------------

    void use() const { glUseProgram(id_); }
    static void unuse() { glUseProgram(0); }

    // ---- uniform 定位与缓存(本类的性能核心)--------------------------------

    // 惰性缓存:首次调驱动查询,之后 O(1)。
    // 返回 -1 表示 uniform 不存在(通常是 shader 里写死了常量被优化掉,
    // 或名字拼错 —— GL 对 location=-1 的写入是【静默忽略】,不报错!)
    [[nodiscard]] GLint uniform(const std::string& name) const {
        if (!valid()) return -1;
        if (const auto it = uniformCache_.find(name); it != uniformCache_.end())
            return it->second;
        const GLint location = glGetUniformLocation(id_, name.c_str());
        uniformCache_.emplace(name, location);
        return location;
    }

    // ---- 类型化 setter(名字对应现有 gl_common.hpp 的自由函数)------------
    // 全部 const + 开头自动 use():
    //   const  = 逻辑上不改 program 本身,但缓存要写 → uniformCache_ 是
    //            mutable(缓存是 std::mutable 的教科书用例);
    //   自动 use() = 消灭"忘了先 use() 写错对象"的经典顺序坑。

    void setMat4(const std::string& name, const glm::mat4& value) const {
        use();
        glUniformMatrix4fv(uniform(name), 1, GL_FALSE, glm::value_ptr(value));
    }

    void setMat3(const std::string& name, const glm::mat3& value) const {
        use();
        glUniformMatrix3fv(uniform(name), 1, GL_FALSE, glm::value_ptr(value));
    }

    void setVec3(const std::string& name, const glm::vec3& value) const {
        use();
        glUniform3fv(uniform(name), 1, glm::value_ptr(value));
    }

    void setVec2(const std::string& name, const glm::vec2& value) const {
        use();
        glUniform2fv(uniform(name), 1, glm::value_ptr(value));
    }

    void setInt(const std::string& name, int value) const {
        use();
        glUniform1i(uniform(name), value);
    }

    void setFloat(const std::string& name, float value) const {
        use();
        glUniform1f(uniform(name), value);
    }

    // ---- 文件加载工厂 --------------------------------------------------------

    // 读两个 shader 文件并构造(SHADER_DIR 宏指向构建目录里同步的资源)。
    // 文件读不到时:打日志,返回无效对象。
    [[nodiscard]] static Program fromFiles(const std::string& vertexPath,
                                           const std::string& fragmentPath) {
        if (const auto vs = readFileOrReport(vertexPath)) {
            if (const auto fs = readFileOrReport(fragmentPath)) {
                return Program{vs->c_str(), fs->c_str()};
            }
        }
        return Program{NoCreate};  // 无效对象(文件读取失败已在上面报告)
    }

private:
    // 注意:GLObject 的 id_ 之外,本类唯一的新增状态就是缓存。
    // move 时基类转移 id_,缓存随本类的默认移动语义一起转移(map 的移动
    // 是 O(1) 指针接管)—— 整个类零手写移动代码。
    mutable std::unordered_map<std::string, GLint> uniformCache_;

    static std::optional<std::string> readFileOrReport(const std::string& path) {
        std::ifstream file{path};
        if (!file) {
            std::fprintf(stderr, "Shader file open failed: %s\n", path.c_str());
            return std::nullopt;
        }
        std::ostringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }
};

} // namespace tutorial::gl
