#include "window.hpp"

#include <cstdio>
#include <utility>

namespace tutorial {
namespace {

void glfwErrorCallback(int error, const char* description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

} // namespace

GlfwContext::GlfwContext() {
    glfwSetErrorCallback(glfwErrorCallback);
    initialized_ = glfwInit() == GLFW_TRUE;
    if (!initialized_) std::fputs("Failed to initialize GLFW\n", stderr);
}

GlfwContext::~GlfwContext() {
    if (initialized_) glfwTerminate();
}

Window::Window(const char* title, int width, int height, bool hidden) {
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, hidden ? GLFW_FALSE : GLFW_TRUE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    handle_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (handle_ == nullptr) {
        std::fputs("An OpenGL 3.3 Core context is required\n", stderr);
        return;
    }
    glfwSetWindowUserPointer(handle_, this);

    makeContextCurrent();
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        std::fputs("Failed to load OpenGL functions\n", stderr);
        glfwDestroyWindow(handle_);
        handle_ = nullptr;
        return;
    }

    glfwSetFramebufferSizeCallback(handle_, &Window::framebufferResizeTrampoline);
    const auto [initialWidth, initialHeight] = framebufferSize();
    onFramebufferSize(initialWidth, initialHeight);
    glfwSwapInterval(1);
    std::printf("OpenGL version: %s\n", glGetString(GL_VERSION));
    std::printf("Renderer: %s\n", glGetString(GL_RENDERER));
}

Window::~Window() {
    if (handle_ != nullptr) glfwDestroyWindow(handle_);
}

Window::Window(Window&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)) {
    if (handle_ != nullptr) glfwSetWindowUserPointer(handle_, this);
}

Window& Window::operator=(Window&& other) noexcept {
    if (this != &other) {
        if (handle_ != nullptr) glfwDestroyWindow(handle_);
        handle_ = std::exchange(other.handle_, nullptr);
        if (handle_ != nullptr) glfwSetWindowUserPointer(handle_, this);
    }
    return *this;
}

bool Window::shouldClose() const {
    return handle_ != nullptr && glfwWindowShouldClose(handle_) == GLFW_TRUE;
}

void Window::requestClose() {
    if (handle_ != nullptr) glfwSetWindowShouldClose(handle_, GLFW_TRUE);
}

bool Window::keyPressed(int key) const {
    return handle_ != nullptr && glfwGetKey(handle_, key) == GLFW_PRESS;
}

void Window::swapBuffers() const {
    if (handle_ != nullptr) glfwSwapBuffers(handle_);
}

void Window::pollEvents() const {
    glfwPollEvents();
}

void Window::makeContextCurrent() const {
    if (handle_ != nullptr) glfwMakeContextCurrent(handle_);
}

std::pair<int, int> Window::framebufferSize() const {
    int width = 0;
    int height = 0;
    if (handle_ != nullptr) glfwGetFramebufferSize(handle_, &width, &height);
    return {width, height};
}

void Window::framebufferResizeTrampoline(GLFWwindow* window, int width, int height) {
    if (auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window))) {
        self->onFramebufferSize(width, height);
    }
}

void Window::onFramebufferSize(int width, int height) {
    glViewport(0, 0, width, height);
}

} // namespace tutorial
