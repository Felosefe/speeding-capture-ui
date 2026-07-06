@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
"D:\QT\Tools\CMake_64\bin\cmake.exe" --preset msvc-debug
if errorlevel 1 exit /b %errorlevel%
"D:\QT\Tools\CMake_64\bin\cmake.exe" --build --preset msvc-debug
