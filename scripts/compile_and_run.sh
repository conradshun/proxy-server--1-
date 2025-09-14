#!/bin/bash

# Compile the proxy server
echo "Compiling proxy server..."
gcc -Wall -Wextra -std=c99 -o proxy_server proxy_server.c

if [ $? -eq 0 ]; then
    echo "Compilation successful!"
    echo "Usage: ./proxy_server <port>"
    echo "Example: ./proxy_server 8080"
    echo ""
    echo "To test the proxy:"
    echo "curl -x localhost:8080 http://www.example.com"
else
    echo "Compilation failed!"
    exit 1
fi
