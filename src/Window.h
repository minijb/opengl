#pragma once
#include <string>
#include <memory>

// 窗口基类， 纯虚类
class Window {
public:
    struct Props
    {
        std::string title   {"Opengl APP"};
        int width           {800};
        int height          {600};
        bool fullscreen     {false};
        bool vsync          {true};
    };
    virtual ~Window() = default;
    virtual void shutdown() = 0;
    virtual bool init(const Props& props = {}) = 0;

    // 主循环
    virtual void pollEvents() = 0;
    virtual void swapBuffers() = 0;
    virtual bool shouldClose() const = 0;
    virtual void setShouldClose(bool v) = 0;

    // 属性
    virtual int  width() const = 0;
    virtual int  height() const = 0;
    virtual void setVsync(bool enabled) = 0;
    // virtual void setEventCallback(EventCallback cb) = 0;
    
    //工厂
    //static create() 工厂：核心代码调 Window::create({...}) 拿到一个 unique_ptr<Window>， 具体是哪种实现，由工厂内部决定——核心根本不需要知道。
    static std::unique_ptr<Window> create(const Props& props = {});
};