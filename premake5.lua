-- premake5.lua
-- MyOpengl 工程描述（与 CMakeLists.txt 等价）
--
-- 用法：
--   Windows: premake5 vs2022   -> 生成 MyOpengl.sln，用 VS 打开
--   Linux:   premake5 gmake2   -> make
--
-- 依赖策略与 CMake 一致：glfw / glew / glm 从 lib/ 下直接编译进工程，
-- 不依赖预编译库。平台文件集与 lib/glfw-3.4/src/CMakeLists.txt 保持一致。

workspace "MyOpengl"
    architecture "x86_64"
    configurations { "Debug", "Release" }
    startproject "MyOpengl"

project "MyOpengl"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++17"
    targetdir "bin/%{cfg.buildcfg}"
    objdir "bin-int/%{cfg.buildcfg}"

    -- 应用源码（对应 CMakeLists 的 GLOB）
    files {
        "src/**.h",
        "src/**.cpp",
        "src/**.c",          -- glad.c
        "include/**.h",
        -- 三方库源码（等价于 add_subdirectory(lib/glfw-3.4) 等）
        "lib/glfw-3.4/src/*.c",
        "lib/glfw-3.4/src/*.m",
        "lib/glew-2.3.1/src/glew.c",
    }

    includedirs {
        "src",
        "include",
        "lib/glfw-3.4/include",
        "lib/glfw-3.4/src",          -- glfw 内部头文件
        "lib/glew-2.3.1/include",
        "lib/glm",
    }

    -- 与 glew_s 的 INTERFACE 编译宏一致（静态链接 GLEW 必需）
    defines { "GLEW_STATIC" }

    filter "configurations:Debug"
        defines { "DEBUG" }
        symbols "On"
        optimize "Off"

    filter "configurations:Release"
        defines { "NDEBUG" }
        symbols "On"
        optimize "Full"

    -- Windows：GLFW Win32 后端（wgl_context.c 属于 win32 组）
    filter "system:windows"
        systemversion "latest"
        defines { "_GLFW_WIN32", "_CRT_SECURE_NO_WARNINGS", "UNICODE", "_UNICODE" }
        links { "opengl32", "gdi32" }
        removefiles {
            "lib/glfw-3.4/src/cocoa_*.c",
            "lib/glfw-3.4/src/cocoa_*.m",
            "lib/glfw-3.4/src/nsgl_context.m",
            "lib/glfw-3.4/src/x11_*.c",
            "lib/glfw-3.4/src/wl_*.c",
            "lib/glfw-3.4/src/posix_*.c",
            "lib/glfw-3.4/src/linux_*.c",
            "lib/glfw-3.4/src/glx_context.c",
            "lib/glfw-3.4/src/xkb_unicode.c",
        }

    -- Linux：GLFW X11 后端（对应 dep.sh 安装的系统依赖）
    filter "system:linux"
        defines { "_GLFW_X11", "_DEFAULT_SOURCE" }
        links { "GL", "X11", "Xrandr", "Xinerama", "Xcursor", "Xi", "Xext", "dl", "m", "pthread" }
        removefiles {
            "lib/glfw-3.4/src/cocoa_*.c",
            "lib/glfw-3.4/src/cocoa_*.m",
            "lib/glfw-3.4/src/nsgl_context.m",
            "lib/glfw-3.4/src/win32_*.c",
            "lib/glfw-3.4/src/wgl_context.c",
            "lib/glfw-3.4/src/wl_*.c",
        }

    -- macOS：GLFW Cocoa 后端（骨架，未实测）
    filter "system:macosx"
        defines { "_GLFW_COCOA" }
        links { "Cocoa.framework", "IOKit.framework", "CoreFoundation.framework", "OpenGL.framework" }
        removefiles {
            "lib/glfw-3.4/src/win32_*.c",
            "lib/glfw-3.4/src/wgl_context.c",
            "lib/glfw-3.4/src/x11_*.c",
            "lib/glfw-3.4/src/wl_*.c",
            "lib/glfw-3.4/src/linux_*.c",
            "lib/glfw-3.4/src/glx_context.c",
            "lib/glfw-3.4/src/posix_time.c",
            "lib/glfw-3.4/src/posix_poll.c",
            "lib/glfw-3.4/src/xkb_unicode.c",
        }

    filter {}
