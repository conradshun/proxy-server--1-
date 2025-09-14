@echo off
echo Compiling proxy server for Windows...

REM Added separate compilation script
gcc -Wall -Wextra -std=c99 -o proxy_server.exe proxy_server.c -lws2_32

if %ERRORLEVEL% EQU 0 (
    echo Compilation successful!
    echo.
    echo Usage: proxy_server.exe ^<port^>
    echo Example: proxy_server.exe 8080
) else (
    echo Compilation failed!
    echo Make sure you have GCC installed and in your PATH.
)

pause
