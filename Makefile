CC = gcc
CFLAGS = -Wall -Wextra -std=c99
TARGET = proxy_server
SOURCE = proxy_server.c

ifeq ($(OS),Windows_NT)
    LDFLAGS = -lws2_32
    TARGET = proxy_server.exe
    RM = del /Q
else
    LDFLAGS = 
    RM = rm -f
endif

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCE) $(LDFLAGS)

clean:
	$(RM) $(TARGET)

.PHONY: clean
