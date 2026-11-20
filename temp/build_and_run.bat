@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /MD /DGLFW_INCLUDE_NONE ^
  /Itemp\include /Iinclude /Iout\build\windows-msvc\vcpkg_installed\x64-windows\include ^
  /Fetemp\build\example.exe temp\example.cpp src\window.cpp ^
  /link /LIBPATH:out\build\windows-msvc\vcpkg_installed\x64-windows\lib glfw3dll.lib glad.lib opengl32.lib legacy_stdio_definitions.lib
