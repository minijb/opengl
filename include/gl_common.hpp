#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace tutorial {

inline bool parseArguments(int argc, char** argv, bool& hidden, int& maxFrames, const char* usage) {
    hidden = false;
    maxFrames = -1;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument == "--hidden") {
            hidden = true;
        } else if (argument == "--max-frames" && i + 1 < argc) {
            maxFrames = std::max(1, std::atoi(argv[++i]));
        } else {
            std::fprintf(stderr, "%s\n", usage);
            return false;
        }
    }
    return true;
}

inline GLuint compileShader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success != GL_TRUE) {
        char log[4096]{};
        glGetShaderInfoLog(shader, static_cast<GLsizei>(sizeof(log)), nullptr, log);
        std::fprintf(stderr, "Shader compilation failed (%s):\n%s\n",
            type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

inline GLuint createProgram(const char* vertexSource, const char* fragmentSource) {
    const GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexSource);
    const GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (vertexShader == 0 || fragmentShader == 0) {
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return 0;
    }
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    GLint success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success != GL_TRUE) {
        char log[4096]{};
        glGetProgramInfoLog(program, static_cast<GLsizei>(sizeof(log)), nullptr, log);
        std::fprintf(stderr, "Program linking failed:\n%s\n", log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

inline void setMat4(GLuint program, const char* name, const glm::mat4& value) {
    glUniformMatrix4fv(glGetUniformLocation(program, name), 1, GL_FALSE, glm::value_ptr(value));
}

inline void setVec3(GLuint program, const char* name, const glm::vec3& value) {
    glUniform3fv(glGetUniformLocation(program, name), 1, glm::value_ptr(value));
}

inline void setInt(GLuint program, const char* name, int value) {
    glUniform1i(glGetUniformLocation(program, name), value);
}

inline void setFloat(GLuint program, const char* name, float value) {
    glUniform1f(glGetUniformLocation(program, name), value);
}

// position.xyz + uv.xy，每个顶点 5 个 float；所有面均为从外面观察时的逆时针绕序。
inline constexpr float cubeVertices[] = {
    -0.5F,-0.5F,-0.5F, 0,0,  0.5F,0.5F,-0.5F, 1,1,  0.5F,-0.5F,-0.5F, 1,0,
    0.5F,0.5F,-0.5F, 1,1,  -0.5F,-0.5F,-0.5F, 0,0,  -0.5F,0.5F,-0.5F, 0,1,
    -0.5F,-0.5F,0.5F, 0,0,  0.5F,-0.5F,0.5F, 1,0,  0.5F,0.5F,0.5F, 1,1,
    0.5F,0.5F,0.5F, 1,1,  -0.5F,0.5F,0.5F, 0,1,  -0.5F,-0.5F,0.5F, 0,0,
    -0.5F,0.5F,0.5F, 1,0,  -0.5F,0.5F,-0.5F, 1,1,  -0.5F,-0.5F,-0.5F, 0,1,
    -0.5F,-0.5F,-0.5F, 0,1,  -0.5F,-0.5F,0.5F, 0,0,  -0.5F,0.5F,0.5F, 1,0,
    0.5F,0.5F,0.5F, 1,0,  0.5F,-0.5F,-0.5F, 0,1,  0.5F,0.5F,-0.5F, 1,1,
    0.5F,-0.5F,-0.5F, 0,1,  0.5F,0.5F,0.5F, 1,0,  0.5F,-0.5F,0.5F, 0,0,
    -0.5F,-0.5F,-0.5F, 0,1,  0.5F,-0.5F,0.5F, 1,0,  0.5F,-0.5F,-0.5F, 1,1,
    0.5F,-0.5F,0.5F, 1,0,  -0.5F,-0.5F,-0.5F, 0,1,  -0.5F,-0.5F,0.5F, 0,0,
    -0.5F,0.5F,-0.5F, 0,1,  0.5F,0.5F,0.5F, 1,0,  0.5F,0.5F,-0.5F, 1,1,
    0.5F,0.5F,0.5F, 1,0,  -0.5F,0.5F,-0.5F, 0,1,  -0.5F,0.5F,0.5F, 0,0,
};

class CubeGeometry {
public:
    CubeGeometry() {
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
            reinterpret_cast<void*>(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);
    }

    CubeGeometry(const CubeGeometry&) = delete;
    CubeGeometry& operator=(const CubeGeometry&) = delete;

    void draw() const {
        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES, 0, 36);
    }

    void destroy() {
        if (vbo_ == 0 && vao_ == 0) return;
        glDeleteBuffers(1, &vbo_);
        glDeleteVertexArrays(1, &vao_);
        vbo_ = 0;
        vao_ = 0;
    }

    ~CubeGeometry() { destroy(); }

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
};

} // namespace tutorial

