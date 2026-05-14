CC      = gcc
CFLAGS  = -Wall -Wextra -g

TARGET  = aishell
SRCS    = shell.c builtins.c execute.c signals.c
OBJS    = $(SRCS:.c=.o)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c shell.h
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: clean
