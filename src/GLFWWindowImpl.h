#pragma once
#include "Window.h"


struct  GLFWwindow; // 前向声明

class GLFWWindowImpl : public Window{
public:
    GLFWWindowImpl() = default;
    ~GLFWWindowImpl() override;
    
    bool init(const Props& props) override;
    void shutdown() override;
    
    void pollEvents() override;
    void swapBuffers() override;
    bool shouldClose() const override;
    void setShouldClose(bool v) override;

    int  width() const override  { return m_width; }
    int  height() const override { return m_height; }
    void setVsync(bool enabled) override;
private:
    static void keyCallback(GLFWwindow* w, int key, int sc, int action, int mods);
    static void framebufferSizeCallback(GLFWwindow* w, int width, int height);
    GLFWwindow*   m_window{nullptr};
    int           m_width{0}, m_height{0};
};