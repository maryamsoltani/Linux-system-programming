CC     = gcc
CFLAGS = -Wall -Wextra -g -I.
LIBS   = -largtable2

# All source files that make up psh
PSH_SRCS = psh.c \
           registry.c \
           cmd_quit.c \
           cmd_jobs.c \
           cmd_bgfg.c \
           cmd_help.c

PSH_OBJS = $(PSH_SRCS:.c=.o)

# Test helper programs
HELPERS = myspin mysplit mystop myint

all: psh $(HELPERS)

psh: $(PSH_OBJS)
	$(CC) $(CFLAGS) -o psh $(PSH_OBJS) $(LIBS)

myspin: myspin.c
	$(CC) $(CFLAGS) -o myspin myspin.c

mysplit: mysplit.c
	$(CC) $(CFLAGS) -o mysplit mysplit.c

mystop: mystop.c
	$(CC) $(CFLAGS) -o mystop mystop.c

myint: myint.c
	$(CC) $(CFLAGS) -o myint myint.c

# Pattern rule: compile any .c to .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f psh $(HELPERS) *.o

.PHONY: all clean
