# temp — GL 资源 RAII 封装（实验区）

调研结论的落地实验，**未接入 CMake**，不影响主工程。

## 文件

- `include/buffer.hpp` — `Buffer`：VBO/EBO 通用 RAII（禁拷贝、破坏性移动、析构自动删）
- `include/mesh.hpp` — `Mesh`：VAO+VBO+EBO 整体单元，同生共死；支持 `glDrawArrays` / `glDrawElements` 两种模式
- `example.cpp` — 冒烟测试：构造/移动/绘制/析构全流程

## 设计规则（调研结论）

1. **禁拷贝 + 破坏性移动**：GLuint 句柄拷贝 = double delete；GL 对象深拷贝不可行
2. **VAO/VBO/EBO 单元化**：删除 buffer 只从当前 context 绑定点解绑，独立生死会留下 VAO 悬空 EBO 引用（GL 4.5 spec §5.1.2）
3. **析构需要 current context**：GL 资源必须声明在 `Window` 之后（栈反向析构：资源先删、窗口后毁）
4. `glDelete*` 对名字 0 静默忽略 → moved-from 置 0 后析构天然安全

## 手动验证

```cmd
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /MD /DGLFW_INCLUDE_NONE ^
  /Itemp\include /Iinclude /Iout\build\windows-msvc\vcpkg_installed\x64-windows\include ^
  /Fetemp\build\example.exe temp\example.cpp src\window.cpp ^
  /link /LIBPATH:out\build\windows-msvc\vcpkg_installed\x64-windows\lib ^
  glfw3dll.lib glad.lib opengl32.lib legacy_stdio_definitions.lib
```

或直接运行 `temp/build_and_run.bat`，产物在 `temp/build/`（运行需把 `glfw3.dll` 放在 exe 旁）。

实测输出：`mesh created (VAO+VBO, draw arrays)` / `mesh created (VAO+VBO+EBO, draw elements)` / `destructive move done` / `3 frames rendered`，exit 0。

成熟参考：Magnum 图形库的 GL 封装层（https://doc.magnum.graphics/magnum/opengl-wrapping.html）。
