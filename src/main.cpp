#include "gl_common.hpp"

namespace {
const char* vertexSource = R"glsl(#version 330 core
layout (location = 0) in vec3 aPos;
uniform vec2 uOffset;
void main() { gl_Position = vec4(aPos.xy + uOffset, aPos.z, 1.0); }
)glsl";
const char* fragmentSource = R"glsl(#version 330 core
out vec4 FragColor;
uniform vec3 uColor;
void main() { FragColor = vec4(uColor, 1.0); }
)glsl";
}

int main(int argc, char** argv) {
    bool hidden = false;
    int maxFrames = -1;
    if (!tutorial::parseArguments(argc, argv, hidden, maxFrames,
            "Usage: part01_07_uniforms [--hidden] [--max-frames N]")) return 2;
    GLFWwindow* window = tutorial::createWindow("Part 01.07 - Uniforms and interaction", hidden);
    if (!window) return 1;
    const GLuint program = tutorial::createProgram(vertexSource, fragmentSource);
    if (!program) return 1;
    const float vertices[] = {-0.3F,-0.25F,0.0F, 0.3F,-0.25F,0.0F, 0.0F,0.35F,0.0F};
    GLuint vao = 0, vbo = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);

    const GLint offsetLocation = glGetUniformLocation(program, "uOffset");
    const GLint colorLocation = glGetUniformLocation(program, "uColor");
    glm::vec2 offset{0.0F};
    std::puts("Arrow keys: move; R: reset position; Esc: exit");
    int frame = 0;
    while (!glfwWindowShouldClose(window)) {
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(window, GLFW_TRUE);
        const float step = 0.008F;
        if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) offset.x -= step;
        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) offset.x += step;
        if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) offset.y -= step;
        if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) offset.y += step;
        if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) offset = glm::vec2{0.0F};
        offset = glm::clamp(offset, glm::vec2{-0.65F}, glm::vec2{0.65F});
        const float time = static_cast<float>(glfwGetTime());
        const glm::vec3 color{0.5F + 0.5F * std::sin(time), 0.35F, 0.8F - 0.3F * std::sin(time)};

        glClearColor(0.06F, 0.07F, 0.10F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(program);
        glUniform2f(offsetLocation, offset.x, offset.y);
        glUniform3fv(colorLocation, 1, glm::value_ptr(color));
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glfwSwapBuffers(window);
        glfwPollEvents();
        if (maxFrames > 0 && ++frame >= maxFrames) glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(program);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

