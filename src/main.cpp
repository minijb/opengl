#include "gl_common.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include "texture_common.hpp"

namespace {
const char* vertexSource = R"glsl(#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aUV;
out vec2 vUV;
uniform mat4 uModel, uView, uProjection;
void main() { gl_Position = uProjection * uView * uModel * vec4(aPos, 1.0); vUV = aUV; }
)glsl";
const char* fragmentSource = R"glsl(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uTexture;
void main() { FragColor = texture(uTexture, vUV); }
)glsl";
}

int main(int argc, char** argv) {
    bool hidden = false;
    int maxFrames = -1;
    if (!tutorial::parseArguments(argc, argv, hidden, maxFrames,
            "Usage: part01_13_depth_culling [--hidden] [--max-frames N]")) return 2;
    GLFWwindow* window = tutorial::createWindow("Part 01.13 - Depth and culling", hidden);
    if (!window) return 1;
    const GLuint program = tutorial::createProgram(vertexSource, fragmentSource);
    const GLuint texture = tutorial::loadTexture(std::string(TEXTURE_DIR) + "/checker.ppm");
    if (!program || !texture) return 1;
    tutorial::CubeGeometry cube;
    glUseProgram(program); tutorial::setInt(program, "uTexture", 0);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW);
    std::puts("D: depth test; C: back-face culling; F: front winding; Esc: exit");
    bool depth = true, culling = true, ccw = true;
    bool previousD = false, previousC = false, previousF = false;
    int frame = 0;
    while (!glfwWindowShouldClose(window)) {
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(window, GLFW_TRUE);
        const bool d = glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS;
        const bool c = glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS;
        const bool f = glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS;
        if (d && !previousD) depth = !depth;
        if (c && !previousC) culling = !culling;
        if (f && !previousF) ccw = !ccw;
        previousD = d; previousC = c; previousF = f;
        if (depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        if (culling) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
        glFrontFace(ccw ? GL_CCW : GL_CW);
        int width = 0, height = 0; glfwGetFramebufferSize(window, &width, &height);
        const glm::mat4 view = glm::lookAt(glm::vec3{3.0F, 2.0F, 5.0F}, glm::vec3{0.0F}, glm::vec3{0.0F,1.0F,0.0F});
        const glm::mat4 projection = glm::perspective(glm::radians(45.0F), static_cast<float>(width) / std::max(height, 1), 0.1F, 100.0F);
        glClearColor(0.05F, 0.06F, 0.10F, 1.0F); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(program); tutorial::setMat4(program, "uView", view); tutorial::setMat4(program, "uProjection", projection);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texture);
        const glm::vec3 positions[] = {{0.0F,0.0F,0.0F}, {0.55F,0.25F,-0.65F}};
        for (const glm::vec3& position : positions) {
            glm::mat4 model{1.0F}; model = glm::translate(model, position);
            tutorial::setMat4(program, "uModel", model); cube.draw();
        }
        glfwSwapBuffers(window); glfwPollEvents();
        if (maxFrames > 0 && ++frame >= maxFrames) glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
    cube.destroy(); glDeleteTextures(1, &texture); glDeleteProgram(program);
    glfwDestroyWindow(window); glfwTerminate();
    return 0;
}

