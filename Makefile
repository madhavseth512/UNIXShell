CC      := gcc
CFLAGS  := -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wshadow -g
TARGET  := msh
SRCS    := $(wildcard *.c)
OBJS    := $(SRCS:.c=.o)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

test: $(TARGET)
	bash tests/all.sh

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: run test clean

: msh.h
