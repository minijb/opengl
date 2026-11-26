// LearnOpenGL 4.2.textures_combined: 双纹理混合 —— Texture2D(CRTP)首次正式登场
//
// 对比手写版, 本课的三块封装分工:
//   Texture2D::fromFile        ← stbi 加载 + 采样参数 + 上传 + mipmap("构造即完成全部参数")
//   索引 Mesh(EBO 首次登场)    ← 第一张"共享顶点"网格, glDrawElements 路线
//   program.setInt(名字, 单元) ← glUniform1i: sampler 存的是【纹理单元编号】
// GLFW 窗口 -> glad -> Program -> Texture2D x2 -> 索引 Mesh -> 渲染循环 -> 自动清理
#include <glad/glad.h>       // 必须在 GLFW 之前
#include <GLFW/glfw3.h>

#include "gl_mesh.hpp"       // Mesh(VAO+VBO+EBO 组合所有权)
#include "gl_program.hpp"    // Program / Shader(GLObject CRTP 派生)
#include "gl_texture.hpp"    // Texture2D(GLObject CRTP 派生)

#include <cstdio>

namespace t = tutorial::gl;   // 命名空间别名(仅本文件作用域)

// CMake 已把 shader/、texture/ 复制到 exe 同级目录
#define SHADER(rel)   SHADER_DIR "/" rel
#define TEXTURE(rel)  TEXTURE_DIR "/" rel

// 四边形: 每顶点 8 个 float(位置 vec3 + 颜色 vec3 + uv vec2),
// 与着色器里 location 0 的 aPos、1 的 aColor、2 的 aTexCoord 对应
float vertices[] = {
    // 位置                // 颜色             // uv
     0.5f,  0.5f, 0.0f,   1.0f, 0.0f, 0.0f,   1.0f, 1.0f,   // 右上
     0.5f, -0.5f, 0.0f,   0.0f, 1.0f, 0.0f,   1.0f, 0.0f,   // 右下
    -0.5f, -0.5f, 0.0f,   0.0f, 0.0f, 1.0f,   0.0f, 0.0f,   // 左下
    -0.5f,  0.5f, 0.0f,   1.0f, 1.0f, 0.0f,   0.0f, 1.0f,   // 左上
};
// EBO: 4 个顶点拼出 2 个三角形(共享对角线顶点, 交错布局下省内存);
// draw 时由 glDrawElements 按下标取顶点, Mesh::draw 内部自动走索引路径
unsigned int indices[] = {
    0, 1, 3,   // 三角形 1: 右上-右下-左上
    1, 2, 3,   // 三角形 2: 右下-左下-左上
};

void framebufferSizeCallback(GLFWwindow* /*window*/, int width, int height)
{
    glViewport(0, 0, width, height);
}

void processInput(GLFWwindow* window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
}

int main()
{
    // ---- 1. GLFW 初始化 + 创建窗口 ----
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(800, 600, "4.2.textures_combined", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);      // 有上下文后才能调 gl* 函数
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);

    // ---- 2. glad: 加载 OpenGL 函数指针 ----
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::fprintf(stderr, "Failed to initialize GLAD\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    // ---- 3+4. 着色器: 一个工厂调用 = 读文件 + 编译 + 链接 ----
    const auto program = t::Program::fromFiles(
        SHADER("1.getting_started/4.2.textures_combined/4.2.texture.vs"),
        SHADER("1.getting_started/4.2.textures_combined/4.2.texture.fs"));
    if (!program) {
        std::fprintf(stderr, "Failed to create shader program\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    // ---- 5. 纹理(本课主角): 两张图, 两个单元 ----
    // container.jpg  = JPEG, 3 通道   → 自动映射 GL_RGB;
    // awesomeface.png = PNG,  4 通道   → 自动映射 GL_RGBA(通道映射的实测)。
    // flipVertically 默认开: 图片自上而下存行, GL 的 UV 原点在左下。
    // 失败得"无效对象"(日志已在工厂里打到 stderr)。
    const auto container   = t::Texture2D::fromFile(TEXTURE("1.getting_started/container.jpg"));
    const auto awesomeface = t::Texture2D::fromFile(TEXTURE("1.getting_started/awesomeface.png"));
    if (!container || !awesomeface) {
        std::fprintf(stderr, "Failed to load textures\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    // ---- 6. Mesh: 索引网格(EBO 首次登场)----
    // 构造内部: vao.bind → vbo 绑定+上传 → 逐条写属性表 → ebo 绑定+上传
    // (EBO 的绑定属于 VAO 状态, 必须在 VAO 绑定期间做)→ unbind VAO。
    // indexType = GL_UNSIGNED_INT: indices 数组元素是 unsigned int。
    const t::Mesh quad{vertices, sizeof(vertices),
        indices, sizeof(indices), GL_UNSIGNED_INT, {
        {0, 3, 8 * sizeof(float), 0},                  // aPos:     3 float, 偏移 0
        {1, 3, 8 * sizeof(float), 3 * sizeof(float)},  // aColor:   3 float, 偏移 3 float
        {2, 2, 8 * sizeof(float), 6 * sizeof(float)},  // aTexCoord 2 float, 偏移 6 float
    }};

    // ---- 7. 纹理单元接线(本课唯一的新知识点)----
    // "绑定"是双层动作: 先选单元(glActiveTexture), 再把纹理挂上去(glBindTexture);
    // sampler uniform 的值 = 【单元编号】(GLint), 不是纹理 id!
    //   sampler texture1 在 shader 里读"0 号单元上挂着的纹理"。
    // 只需接线一次: 单元→纹理 的绑定是全局状态, 没人动它就一直有效。
    container.bind(0);    // container   → 0 号单元
    awesomeface.bind(1);  // awesomeface → 1 号单元
    program.setInt("texture1", 0);  // 告诉 texture1 去 0 号单元取
    program.setInt("texture2", 1);  // 告诉 texture2 去 1 号单元取

    // ---- 8. 渲染循环 ----
    while (!glfwWindowShouldClose(window)) {
        processInput(window);

        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        quad.draw();   // 内部: bind VAO → glDrawElements(索引路径)

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // ---- 9. 清理: RAII 全自动 ----
    // 析构逆序: quad(VAO 先死解除引用 → EBO → VBO)→ awesomeface/container
    // (glDeleteTextures)→ program(glDeleteProgram)。GL 上下文此刻仍有效。
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
