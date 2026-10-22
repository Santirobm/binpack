@echo off
if exist "%~dp0binpack.exe" (
    "%~dp0binpack.exe" %*
) else (
    python "%~dp0binpack.py" %*
)
