#pragma once
#include "Window.h"

class Application{
public:
    std::unique_ptr<Window> m_window;
    void run();
private:
    void render();
};