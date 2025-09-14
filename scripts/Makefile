CC = gcc
CFLAGS = -Wall -Wextra -std=c99
TARGET = proxy_server
SOURCE = proxy_server.c

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCE)

clean:
	rm -f $(TARGET)

.PHONY: clean
