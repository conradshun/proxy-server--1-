#!/bin/bash

# Compile the proxy server
make

# Check if compilation was successful
if [ $? -eq 0 ]; then
    echo "Compilation successful!"
    if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" ]]; then
        echo "Usage: ./proxy_server.exe <port>"
        echo "Example: ./proxy_server.exe 8080"
    else
        echo "Usage: ./proxy_server <port>"
        echo "Example: ./proxy_server 8080"
    fi
else
    echo "Compilation failed!"
    exit 1
fi
