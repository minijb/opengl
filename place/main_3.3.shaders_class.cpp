// LearnOpenGL 3.3.shaders_class: CRTP/RAII 封装版
//
// [学习记录] 本文件是 src/main.cpp 的历史版本快照, 不参与构建。
// 它记录的是"手写流程 → CRTP 封装"的第一次重写:
// 把裸 GL 调用替换为 include/ 里 GLObject<CRTP> 家族的使用方式。
//
// 与手写版逐项对照(include/ 里的封装替换裸流程):
//   Program::fromFiles   ← ReadFile + compileShader + buildProgram + 手动删 program
//   Mesh                 ← 两大块 glGen*/bind/AttribPointer 属性配置 + 手动删 VAO/VBO
//   program.setVec3      ← 每帧 glGetUniformLocation(字符串查找)+ glUniform3f
//   作用域结束自动析构   ← main 末尾一串 glDeleteXXX
// GLFW 窗口 -> glad 加载 -> Program -> Mesh -> 渲染循环 -> 自动清理
#include <glad/glad.h>       // 必须在 GLFW 之前
#include <GLFW/glfw3.h>

#include "gl_mesh.hpp"       // Mesh(VAO+VBO+EBO 组合所有权)
#include "gl_program.hpp"    // Program / Shader(GLObject CRTP 派生)

#include <cmath>
#include <cstdio>

// 命名空间别名: tutorial::gl -> t(仅在本文件作用域, 不污染头文件)
namespace t = tutorial::gl;

// CMake 已把 shader/ 复制到 exe 同级目录, SHADER_DIR 宏指向那里
#define SHADER(rel) SHADER_DIR "/" rel

// 三角形: 每顶点 6 个 float(位置 vec3 + 颜色 vec3),
// 与着色器里 location 0 的 aPos、location 1 的 aColor 对应
float vertices[] = {
    // 位置              // 颜色
     0.5f, -0.5f, 0.0f,  1.0f, 0.0f, 0.0f,   // 右下 红
    -0.5f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f,   // 左下 绿
     0.0f,  0.5f, 0.0f,  0.0f, 0.0f, 1.0f,   // 顶部  蓝
};

float vertices1[] = {
    // 位置              // 颜色
     0.1f, -0.5f, 0.0f,  1.0f, 0.0f, 0.0f,   // 右下 红
    -0.1f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f,   // 左下 绿
     0.1f,  0.5f, 0.0f,  0.0f, 0.0f, 1.0f,   // 顶部  蓝
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

    GLFWwindow* window = glfwCreateWindow(800, 600, "3.3.shaders_class", nullptr, nullptr);
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
    // 失败不抛异常: 得到"无效对象"(InfoLog 已在内部打到 stderr),
    // 用 if (program) 检查 —— 对应手写版 buildProgram() 返回 0。
    // Shader(编译产物)只是 Program 构造里的临时零件, 装配完即删(RAII)。
    const auto program = t::Program::fromFiles(
        SHADER("1.getting_started/3.3.shaders_class/3.3.shader.vs"),
        SHADER("1.getting_started/3.3.shaders_class/3.3.shader.fs"));
    if (!program) {
        std::fprintf(stderr, "Failed to create shader program\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    // ---- 5. Mesh: VAO + VBO 同生共死 ----
    // 手写版两大块"gen -> bind -> upload -> 逐条 AttribPointer"全部收进
    // Mesh 构造: 属性表每行 {槽位, 分量数, 步长, 字节偏移}。
    //   location 0 = aPos(3 个 float, 偏移 0)
    //   location 1 = aColor(3 个 float, 偏移 3*4, 步长是整顶点 6*4)
    const t::Mesh mesh1{vertices, sizeof(vertices), {
        {0, 3, 6 * sizeof(float), 0},
        {1, 3, 6 * sizeof(float), 3 * sizeof(float)},
    }};
    const t::Mesh mesh2{vertices1, sizeof(vertices1), {
        {0, 3, 6 * sizeof(float), 0},
        {1, 3, 6 * sizeof(float), 3 * sizeof(float)},
    }};

    // GLObject<CRTP> 给 Mesh/Program/Buffer/... 带来的 move-only 语义:
    //   t::Mesh copy = mesh1;  // 编译错误: 拷贝被删(防 double-delete)
    //   auto m = std::move(mesh1);        // OK: 所有权转移
    // 移动构造内部就是 std::exchange(other.id_, 0) —— 读走句柄的同时置空源;
    // moved-from 对象析构安全(reset() 对 id 0 是空操作), 但不能再 draw。

    bool isLine = false;

    // ---- 6. 渲染循环 ----
    while (!glfwWindowShouldClose(window)) {
        processInput(window);

        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
            isLine = !isLine;

        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glPolygonMode(GL_FRONT_AND_BACK, isLine ? GL_LINE : GL_FILL);

        // uniform: setter 内部自动 use()(防"忘了先 use 写错对象"的经典坑),
        // location 惰性缓存 —— 不再每帧做 glGetUniformLocation 字符串查找
        const float timeValue = static_cast<float>(glfwGetTime());
        const float greenValue = std::sin(timeValue) / 2.0f + 0.5f;
        program.setVec3("uniColor", {0.0f, greenValue, 0.0f});

        // draw 内部: bind VAO + glDrawArrays(非索引网格)
        mesh1.draw();
        mesh2.draw();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // ---- 7. 清理: RAII 全自动 ----
    // main 作用域结束时按声明逆序析构: mesh2 → mesh1 → program。
    // Mesh 内部成员声明顺序 vbo_/ebo_/vao_ → 析构 vao_ 先死(它对 buffer 的
    // 引用随之蒸发)→ vbo_/ebo_ 再删, 悬空引用结构性不可能。
    // 手写版末尾的 glDeleteVertexArrays/glDeleteBuffers/glDeleteProgram 一行都不剩。
    // 此刻 GL 上下文仍有效(局部对象的析构先于下面的 glfwDestroyWindow)。
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
