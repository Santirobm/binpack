@echo off
rem Build script for BINPACK Native Engine (Windows)

setlocal
where cl >nul 2>nul
if %ERRORLEVEL% equ 0 (
    echo [BUILD] Compiling with MSVC cl.exe...
    cl /O2 /W3 binpack.c test_native.c /Fe:test_native.exe
    if %ERRORLEVEL% equ 0 (
        echo [OK] Build complete: test_native.exe
        exit /b 0
    )
)

where gcc >nul 2>nul
if %ERRORLEVEL% equ 0 (
    echo [BUILD] Compiling with GCC...
    gcc -O3 -Wall binpack.c test_native.c -o test_native.exe
    if %ERRORLEVEL% equ 0 (
        echo [OK] Build complete: test_native.exe
        exit /b 0
    )
)

where clang >nul 2>nul
if %ERRORLEVEL% equ 0 (
    echo [BUILD] Compiling with Clang...
    clang -O3 -Wall binpack.c test_native.c -o test_native.exe
    if %ERRORLEVEL% equ 0 (
        echo [OK] Build complete: test_native.exe
        exit /b 0
    )
)

echo [ERROR] No supported C compiler (MSVC, GCC, Clang) found in PATH.
exit /b 1
