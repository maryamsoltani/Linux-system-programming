# ─────────────────────────────────────────────────────────────────────────────
# AiShell — Makefile
# Builds a single binary: aishell
# Each command follows the cmd_spec_t anatomy (CommandAnatomy.ipynb)
# ─────────────────────────────────────────────────────────────────────────────

CC      = gcc
CFLAGS  = -Wall -Wextra -g -I./argtable3
TARGET  = aishell

# argtable3 (vendored argument parsing library)
ARGTABLE_SRC = $(wildcard argtable3/*.c)
ARGTABLE_OBJ = $(ARGTABLE_SRC:.c=.o)

# Command modules — one anatomy per file (or grouped logically)
CMD_SRC = \
    src/cmd_ls.c       \
    src/cmd_stat.c     \
    src/cmd_cat.c      \
    src/cmd_headtail.c \
    src/cmd_fileops.c  \
    src/cmd_shell.c    \
    src/cmd_rg.c       \
    src/cmd_edit.c

CMD_OBJ = $(CMD_SRC:.c=.o)
MAIN_OBJ = main.o
ALL_OBJ  = $(ARGTABLE_OBJ) $(CMD_OBJ) $(MAIN_OBJ)

.PHONY: all clean symlinks test

all: $(TARGET)

$(TARGET): $(ALL_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ -lm
	@echo "Build successful: ./$(TARGET)"

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Create per-command symlinks so each can be invoked directly (e.g. ./ls)
CMDS = ls stat cat head tail cp mv rm mkdir rmdir touch \
       pwd cd env export unset type rg \
       edit-replace-line edit-insert-line edit-delete-line edit-replace

symlinks: $(TARGET)
	@for cmd in $(CMDS); do \
	    ln -sf $(TARGET) $$cmd; \
	    echo "  linked: $$cmd -> $(TARGET)"; \
	done

# Quick smoke test
test: $(TARGET)
	@echo "=== Registry ==="
	@./$(TARGET) --list
	@echo ""
	@echo "=== pwd ==="
	@./$(TARGET) pwd
	@echo ""
	@echo "=== ls (JSON) ==="
	@./$(TARGET) ls --json .
	@echo ""
	@echo "=== touch + stat (JSON) ==="
	@./$(TARGET) touch /tmp/aishell_test.txt
	@./$(TARGET) stat --json /tmp/aishell_test.txt
	@echo ""
	@echo "=== edit commands ==="
	@echo -e "line one\nline two\nline three" > /tmp/aishell_edit_test.txt
	@./$(TARGET) edit-replace-line /tmp/aishell_edit_test.txt 2 "REPLACED"
	@./$(TARGET) cat /tmp/aishell_edit_test.txt
	@./$(TARGET) edit-insert-line /tmp/aishell_edit_test.txt 1 "INSERTED"
	@./$(TARGET) cat /tmp/aishell_edit_test.txt
	@./$(TARGET) edit-delete-line /tmp/aishell_edit_test.txt 3
	@./$(TARGET) cat /tmp/aishell_edit_test.txt
	@./$(TARGET) edit-replace --json /tmp/aishell_edit_test.txt "line" "LINE"
	@./$(TARGET) cat /tmp/aishell_edit_test.txt
	@echo ""
	@echo "=== rg (JSON) ==="
	@./$(TARGET) rg --json --fixed-strings "LINE" /tmp/aishell_edit_test.txt
	@echo ""
	@echo "All tests passed."

clean:
	rm -f $(ALL_OBJ) $(TARGET)
	rm -f $(CMDS)
