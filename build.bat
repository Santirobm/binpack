@echo off
rem Build script for BINPACK Native Engine and CLI (Windows)

setlocal

set CFLAGS=/O2 /W3 /I deps\include
set ZSTD_LIB=deps\lib\libzstd_static.lib

where cl >nul 2>nul
if %ERRORLEVEL% equ 0 (
    echo [BUILD] Compiling with MSVC cl.exe...
    cl %CFLAGS% binpack.c binpack_cli.c %ZSTD_LIB% /Fe:binpack.exe
    cl %CFLAGS% binpack.c test_native.c %ZSTD_LIB% /Fe:test_native.exe
    if %ERRORLEVEL% equ 0 (
        echo [OK] Build complete: binpack.exe and test_native.exe
        exit /b 0
    )
)

where zig >nul 2>nul
if %ERRORLEVEL% equ 0 (
    echo [BUILD] Compiling with Zig CC...
    zig cc -O3 -Wall -Ideps/include binpack.c binpack_cli.c deps/lib/libzstd_static.lib -o binpack.exe
    zig cc -O3 -Wall -Ideps/include binpack.c test_native.c deps/lib/libzstd_static.lib -o test_native.exe
    if %ERRORLEVEL% equ 0 (
        echo [OK] Build complete: binpack.exe and test_native.exe
        exit /b 0
    )
)

where clang >nul 2>nul
if %ERRORLEVEL% equ 0 (
    echo [BUILD] Compiling with Clang...
    clang -O3 -Wall -Ideps/include binpack.c binpack_cli.c deps/lib/libzstd_static.lib -o binpack.exe
    clang -O3 -Wall -Ideps/include binpack.c test_native.c deps/lib/libzstd_static.lib -o test_native.exe
    if %ERRORLEVEL% equ 0 (
        echo [OK] Build complete: binpack.exe and test_native.exe
        exit /b 0
    )
)

where gcc >nul 2>nul
if %ERRORLEVEL% equ 0 (
    echo [BUILD] Compiling with GCC...
    gcc -O3 -Wall -Ideps/include binpack.c binpack_cli.c deps/lib/libzstd_static.lib -o binpack.exe
    gcc -O3 -Wall -Ideps/include binpack.c test_native.c deps/lib/libzstd_static.lib -o test_native.exe
    if %ERRORLEVEL% equ 0 (
        echo [OK] Build complete: binpack.exe and test_native.exe
        exit /b 0
    )
)

echo [ERROR] No supported C compiler (MSVC, GCC, Clang, Zig CC) found in PATH.
exit /b 1
