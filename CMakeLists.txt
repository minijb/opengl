cmake_minimum_required(VERSION 3.25)

project(CG_Lab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenGL REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)

add_executable(CG_Lab
    src/main.cpp
)

target_compile_definitions(CG_Lab PRIVATE
    GLFW_INCLUDE_NONE
)

target_link_libraries(CG_Lab PRIVATE
    glfw
    glm::glm
    glad::glad
    OpenGL::GL
)
