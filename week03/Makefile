# Makefile — build mish.
#
# Strict warnings on by default.  Override CC or CFLAGS on the command
# line if needed:  make CC=clang CFLAGS='-O2 -Wall'

CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -Wshadow -std=c11 -g

SRCS    = src/mish.c src/token.c src/command.c src/builtin.c
OBJS    = $(SRCS:.c=.o)
HEADERS = src/token.h src/command.h src/builtin.h
TARGET  = mish

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

# Pattern rule: every .o depends on every header (small project, simple is fine)
src/%.o: src/%.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TARGET)
	./tests/test.sh

clean:
	rm -f $(OBJS) $(TARGET)
