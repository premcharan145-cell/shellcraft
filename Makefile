CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -D_POSIX_C_SOURCE=200809L -Iinclude

TARGET = shellcraft

SRC = src/main.c src/parser.c src/jobs.c src/execute.c

all: $(TARGET)

$(TARGET): $(SRC) include/shell.h
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run
