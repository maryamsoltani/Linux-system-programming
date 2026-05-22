CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -pthread -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700

TARGET  = aishell

SRC = main.c \
      cmd_localdate.c \
      cmd_ls.c \
      cmd_cat.c \
      cmd_pkg.c \
      cmd_pwd.c \
      cmd_wc.c \
      cmd_touch.c \
      cmd_mkdir.c \
      cmd_rmdir.c \
      cmd_echo.c \
      cmd_whoami.c \
      cmd_clear.c \
      cmd_id.c \
      cmd_uname.c \
      cmd_head.c \
      cmd_tail.c \
      cmd_cp.c \
      cmd_mv.c \
      cmd_rm.c \
      cmd_dirname.c \
      cmd_du.c \
      cmd_procinfo.c \
      cmd_threads.c \
      registry.c \
      register_all_commands.c \
      json_utils.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)
	find . -name '*.o' -delete

run: all
	./$(TARGET)

test: all
	./$(TARGET) localdate
	./$(TARGET) ls
	./$(TARGET) echo hello world
	./$(TARGET) pwd
	./$(TARGET) id
	./$(TARGET) whoami
	./$(TARGET) uname
	./$(TARGET) procinfo
	./$(TARGET) threads -n 2

.PHONY: all clean run test
