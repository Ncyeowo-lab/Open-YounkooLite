@echo off
setlocal
if not defined VCVARS set VCVARS=auto

cmake -B build -S . || goto :err
cmake --build build --config Release || goto :err

echo.
echo Build succeeded: build\Release\openyounkoolite.exe
exit /b 0

:err
echo.
echo Build FAILED. If you don't have Visual Studio, use build-zig.bat instead.
exit /b 1
