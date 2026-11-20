#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <utility>

namespace tutorial {

// GLFW 库生命周期（glfwInit / glfwTerminate）。
// 注意：GLFW 要求所有窗口都在 glfwTerminate() 之前销毁，
// 所以在 main() 里先声明它、再声明 Window，析构顺序即自动正确。
class GlfwContext {
public:
    GlfwContext();
    ~GlfwContext();

    GlfwContext(const GlfwContext&) = delete;
    GlfwContext& operator=(const GlfwContext&) = delete;

    explicit operator bool() const { return initialized_; }
    bool valid() const { return initialized_; }

private:
    bool initialized_ = false;
};

// GLFWwindow 的 RAII 封装：构造时创建窗口并初始化 OpenGL 上下文，
// 析构时自动 glfwDestroyWindow。不可复制、可移动。
class Window {
public:
    // 默认构造一个无效窗口，之后可用移动赋值接手
    Window() = default;
    Window(const char* title, int width, int height, bool hidden = false);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;

    explicit operator bool() const { return handle_ != nullptr; }
    bool valid() const { return handle_ != nullptr; }
    GLFWwindow* get() const { return handle_; }

    bool shouldClose() const;
    void requestClose();
    bool keyPressed(int key) const;
    void swapBuffers() const;
    void pollEvents() const;
    void makeContextCurrent() const;
    std::pair<int, int> framebufferSize() const;

private:
    // user-pointer 回调模式：静态回调 -> 从 GLFWwindow 取回 this -> 调成员函数
    static void framebufferResizeTrampoline(GLFWwindow* window, int width, int height);
    void onFramebufferSize(int width, int height);

    GLFWwindow* handle_ = nullptr;
};

} // namespace tutorial
