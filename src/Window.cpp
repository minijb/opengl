#include "Window.h"
#include "GLFWWindowImpl.h"

std::unique_ptr<Window> Window::create(const Props& props) {
    auto window = std::make_unique<GLFWWindowImpl>();
    if (!window->init(props)) return nullptr;   // 创建失败返回空，调用方自行处理
    return window;
}