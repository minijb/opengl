#include <glad/glad.h>      // 必须在 GLFW 之前（GL 头依赖顺序）
#include <GLFW/glfw3.h>

int main() {
    glfwInit();                                          // ① 初始化 GLFW
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);       // ② 要求 GL 3.3
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);       //    core profile（无旧管线）
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(800, 600, "opengl-hello", nullptr, nullptr);
    glfwMakeContextCurrent(window);

    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);  // ③ 绑定驱动函数指针

    // ④ VAO/VBO 上传顶点、编译链接 shader（GLSL 330 core）
    // ⑤ 渲染循环：clear → draw → swap → poll
    while (!glfwWindowShouldClose(window)) { /* … */ }
    glfwTerminate();
}