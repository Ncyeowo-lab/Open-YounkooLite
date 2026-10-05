@echo off
setlocal enabledelayedexpansion

set ZIG=
for %%P in (
    "%~dp0zig.exe"
    "%LOCALAPPDATA%\Programs\Python\Python313\Scripts\python-zig.exe"
) do if exist %%~P set ZIG=%%~P

if not defined ZIG (
    for /f "delims=" %%i in ('where python-zig 2^>nul') do if not defined ZIG set ZIG=%%i
)
if not defined ZIG (
    for /f "delims=" %%i in ('where zig 2^>nul') do if not defined ZIG set ZIG=%%i
)
if not defined ZIG (
    echo zig not found. Install it with:  pip install ziglang
    exit /b 1
)

echo using %ZIG%
if not exist build-zig mkdir build-zig

set FLAGS=-target x86_64-windows-gnu -O1 -I third_party -I third_party/lua -I src -D_WIN32_WINNT=0x0A00 -DWIN32_LEAN_AND_MEAN -DNOMINMAX

echo.
echo [1/3] compiling Lua 5.4.7 (C)...
for %%f in (third_party\lua\*.c) do (
    "%ZIG%" cc -target x86_64-windows-gnu -O1 -I third_party/lua -c "%%f" -o "build-zig\%%~nf.o" || exit /b 1
)

echo.
echo [2/3] compiling project (C++17)...
for %%f in (src\*.cpp) do (
    echo   %%~nf
    "%ZIG%" c++ %FLAGS% -std=c++17 -c "%%f" -o "build-zig\%%~nf.o" || exit /b 1
)

echo.
echo [3/3] linking...
"%ZIG%" c++ -target x86_64-windows-gnu -o build-zig\openyounkoolite.exe build-zig\*.o -lws2_32 -lpsapi -lkernel32 -luser32 || exit /b 1

echo.
echo Build succeeded: build-zig\openyounkoolite.exe
exit /b 0
