// LearnOpenGL 3.3.shaders_class: 手写流程
// GLFW 窗口 -> glad 加载 -> ReadFile(std::optional) -> 编译/链接着色器
// -> VAO/VBO -> 渲染循环 -> 清理
#include <glad/glad.h>       // 必须在 GLFW 之前
#include <GLFW/glfw3.h>

#include <cstdio>

#include "tools.hpp"

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

// 编译单个着色器; 失败时打印 InfoLog 并返回 0
unsigned int compileShader(GLenum type, const char* source)
{
    const unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);   // 注意: 传 &source(指针的地址)
    glCompileShader(shader);

    int success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "ERROR::SHADER::COMPILE_FAILED (%s)\n%s\n",
                     type == GL_VERTEX_SHADER ? "VERTEX" : "FRAGMENT", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// 编译并链接 VS + FS; 成功返回 program, 失败返回 0
unsigned int buildProgram(const char* vsSource, const char* fsSource)
{
    const unsigned int vs = compileShader(GL_VERTEX_SHADER, vsSource);
    const unsigned int fs = compileShader(GL_FRAGMENT_SHADER, fsSource);
    if (vs == 0 || fs == 0) {
        glDeleteShader(vs);
        glDeleteShader(fs);
        return 0;
    }

    const unsigned int program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    // 着色器对象链接进 program 之后就没用了, 立刻删
    glDeleteShader(vs);
    glDeleteShader(fs);

    int success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::fprintf(stderr, "ERROR::PROGRAM::LINK_FAILED\n%s\n", log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
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

    // ---- 3. 读着色器源码(ReadFile 返回 std::optional, 失败要提前退出) ----
    const auto vsSource = ReadFile(SHADER("1.getting_started/3.3.shaders_class/3.3.shader.vs"));
    const auto fsSource = ReadFile(SHADER("1.getting_started/3.3.shaders_class/3.3.shader.fs"));
    if (!vsSource || !fsSource) {
        std::fprintf(stderr, "Failed to read shader files\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    // ---- 4. 编译 + 链接 ----
    const unsigned int shaderProgram = buildProgram(vsSource->c_str(), fsSource->c_str());
    if (shaderProgram == 0) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    // ---- 5. VAO + VBO ----
    unsigned int VBO = 0, VAO = 0;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);              // 记录后续属性配置的"容器"
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // location 0: aPos,   偏移 0,     3 个 float
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // location 1: aColor, 偏移 3 float, 3 个 float(步长是整顶点 6 float)
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    
    unsigned int VBO1, VAO1;
    glGenVertexArrays(1, &VAO1);
    glGenBuffers(1, &VBO1);
    
    glBindVertexArray(VAO1);
    glBindBuffer(GL_ARRAY_BUFFER, VBO1);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices1), vertices1, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    
    bool isLine = false;
    

    // ---- 6. 渲染循环 ----
    while (!glfwWindowShouldClose(window)) {
        processInput(window);
        
        if(glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS){
            isLine = !isLine;
        }

        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        
        if (isLine)
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        else
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        float timeValue = glfwGetTime();
        float greenVlaue = (sin(timeValue) / 2.0f) + 0.5f;
        int vertexColorLocation = glGetUniformLocation(shaderProgram, "uniColor");

        glUseProgram(shaderProgram);
        glUniform3f(vertexColorLocation, 0.0f, greenVlaue, 0.0f);       
        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(VAO1);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // ---- 7. 清理 ----
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(shaderProgram);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
