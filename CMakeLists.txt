cmake_minimum_required(VERSION 3.25)

project(CG_Lab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)  # 源码为 UTF-8（无 BOM），避免 C4819 代码页误判
endif()

find_package(OpenGL REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)

add_executable(CG_Lab
    src/main.cpp
    src/tools.cpp
    src/window.cpp
    src/stb_impl.cpp
)

target_include_directories(CG_Lab PUBLIC
    include
)

target_compile_definitions(CG_Lab PRIVATE
    GLFW_INCLUDE_NONE
    # 指向构建目录中被复制后的资源(与 exe 同级)
    # $<TARGET_FILE_DIR:...> 会展开为 out/build/<preset>/Debug 或 /Release
    SHADER_DIR="$<TARGET_FILE_DIR:CG_Lab>/shader"
    TEXTURE_DIR="$<TARGET_FILE_DIR:CG_Lab>/texture"
    MODEL_DIR="$<TARGET_FILE_DIR:CG_Lab>/model"
)

# ---- 资源目录(shader / texture / model)自动复制到构建目录 ----
# 新增资源目录时只需在此追加目录名,宏用 target_compile_definitions 里的同名变量
set(CG_LAB_ASSET_DIRS
    shader
    texture
    model
)

# 每次构建成功后,把各资源目录同步到 exe 所在目录。
# 生成器表达式保证多配置生成器(VS)下自动落到 Debug/ 或 Release/ 子目录。
foreach(dir IN LISTS CG_LAB_ASSET_DIRS)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${dir}")
        add_custom_command(TARGET CG_Lab POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/${dir}"
                "$<TARGET_FILE_DIR:CG_Lab>/${dir}"
            COMMENT "同步 ${dir}/ 到构建目录"
            VERBATIM
        )
    endif()
endforeach()

target_link_libraries(CG_Lab PRIVATE
    glfw
    glm::glm
    glad::glad
    OpenGL::GL
)
