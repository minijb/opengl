cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)   # 编译器不支持就报错，而非静默降级                   
set(CMAKE_CXX_EXTENSIONS OFF)         # 用 -std=c++17 而非 -std=gnu++17，可移植  
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(MyOpengl)

find_package(OpenGL REQUIRED)
add_subdirectory(lib/glfw-3.4)
add_subdirectory(lib/glew-2.3.1/build/cmake)
add_subdirectory(lib/glm)


#判断是否在 为 wsl
if(CMAKE_SYSTEM_NAME STREQUAL "Windows") # 当前系统为 windows
	set(OPENGL_LIB opengl32 gdi32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux") # 当前系统为 Linux
    set(OPENGL_LIB OpenGL::GL)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin") 
endif()

add_executable(MyOpengl src/main.cpp src/glad.c)

target_include_directories(MyOpengl PUBLIC include)
target_include_directories(MyOpengl PUBLIC src)
target_link_libraries(MyOpengl glfw)
target_link_libraries(MyOpengl glew_s ${OPENGL_LIB})
target_link_libraries(MyOpengl glm)
