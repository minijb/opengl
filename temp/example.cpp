// temp/ 目录的 RAII 网格封装冒烟测试：
// 复用 gl_common.hpp 的 cubeVertices（position.xyz + uv.xy，36 顶点）验证
// Mesh 构造、破坏性移动、draw、析构全流程。未接入 CMake，手动编译。
#include "buffer.hpp"
#include "gl_common.hpp"
#include "mesh.hpp"
#include "window.hpp"

#include <cstdio>
#include <span>

int main() {
    const tutorial::GlfwContext glfwContext;
    tutorial::Window window("RAII mesh smoke test", 320, 240, true);
    if (!window) return 1;

    constexpr GLsizei stride = 5 * sizeof(float);
    const tutorial::Mesh::Attribute attributes[] = {
        {0, 3, GL_FLOAT, 0},                  // position.xyz
        {1, 2, GL_FLOAT, 3 * sizeof(float)},  // uv.xy
    };
    tutorial::Mesh cube(tutorial::cubeVertices, sizeof(tutorial::cubeVertices), 36,
                        std::span<const tutorial::Mesh::Attribute>{attributes}, stride);
    std::puts("mesh created (VAO+VBO, draw arrays)");

    // 带索引的用例：用顶点索引构建，draw() 自动切换到 glDrawElements
    const tutorial::Mesh::Attribute quadAttributes[] = {
        {0, 3, GL_FLOAT, 0},
    };
    const float quadVertices[] = {
        -0.5F, -0.5F, 0.0F,
         0.5F, -0.5F, 0.0F,
         0.5F,  0.5F, 0.0F,
        -0.5F,  0.5F, 0.0F,
    };
    const GLuint quadIndices[] = {0, 1, 2, 0, 2, 3};
    tutorial::Mesh quad(quadVertices, sizeof(quadVertices), 4,
                        std::span<const tutorial::Mesh::Attribute>{quadAttributes},
                        3 * sizeof(float), quadIndices, std::size(quadIndices));
    std::puts("mesh created (VAO+VBO+EBO, draw elements)");

    // 破坏性移动：cube 的句柄转移给 moved，cube 析构时不再删任何 GL 对象
    tutorial::Mesh movedCube = std::move(cube);
    std::puts("destructive move done");

    for (int frame = 0; frame < 3; ++frame) {
        const auto [width, height] = window.framebufferSize();
        glViewport(0, 0, width, height);
        glClearColor(0.05F, 0.06F, 0.10F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
        movedCube.draw(); // 无 shader 时 draw 是合法 no-op，这里只验证 RAII 生命周期
        quad.draw();
        window.swapBuffers();
        window.pollEvents();
    }
    std::puts("3 frames rendered");
    return 0;
    // 析构顺序（栈反向）：quad -> movedCube -> window -> glfwContext，
    // GL 对象先删、窗口后毁，context 在整个生命周期内保持 current。
}
