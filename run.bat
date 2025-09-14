@echo off
echo Compiling proxy server for Windows...

REM Use gcc directly instead of make for Windows compatibility
gcc -Wall -Wextra -std=c99 proxy_server.c -o proxy_server.exe -lws2_32

if %ERRORLEVEL% EQU 0 (
    echo Compilation successful!
    echo.
    echo Usage: proxy_server.exe ^<port^>
    echo Example: proxy_server.exe 8080
    echo.
    echo Starting proxy server on port 8080...
    proxy_server.exe 8080
) else (
    echo Compilation failed!
    echo Make sure you have GCC installed and in your PATH.
    pause
    exit /b 1
)

pause

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #pragma comment(lib, "ws2_32.lib")
    #define close closesocket
    #define ssize_t int
    typedef int socklen_t;
#endif

#ifdef _WIN32
    #define strdup _strdup
    #define strncasecmp _strnicmp
#endif

void handle_client(int client_socket);
