BNFC    ?= bnfc
GRAMMAR := Grammar.cf

CC      = gcc
CFLAGS  = -g -W -Wall -Wsign-conversion -Wno-unused-parameter \
          -Wno-unused-function -D_POSIX_C_SOURCE=200809L

FLEX    = flex
BISON   = bison

OBJS    = Absyn.o Buffer.o Lexer.o Parser.o Printer.o

.PHONY: all gen build clean run ast shell test

all: build

# ── Step 1: generate sources from the BNFC grammar ───────────────────────────
gen: Grammar.l Grammar.y

Grammar.l Grammar.y &: $(GRAMMAR)
	$(BNFC) --c $(GRAMMAR)
	# Patch Grammar.l: add the 'grammar_' lex-function prefix required for
	# the reentrant scanner so that symbols resolve correctly at link time.
	grep -q 'prefix="grammar_"' Grammar.l || \
	  sed -i 's/%option noyywrap noinput nounput/%option noyywrap noinput nounput\n%option prefix="grammar_"/' Grammar.l
	# Patch Grammar.y: map the bison 'yylex' call to the prefixed 'grammar_lex'.
	grep -q 'define yylex grammar_lex' Grammar.y || \
	  sed -i 's/#define YYMAXDEPTH 10000000/#define YYMAXDEPTH 10000000\n#define yylex grammar_lex/' Grammar.y

# ── Step 2: generate C files from .l and .y ──────────────────────────────────
Lexer.c: Grammar.l
	$(FLEX) -oLexer.c Grammar.l

Parser.c Bison.h: Grammar.y
	$(BISON) -t Grammar.y -o Parser.c

# ── Object files ─────────────────────────────────────────────────────────────
Absyn.o: Absyn.c Absyn.h
	$(CC) $(CFLAGS) -c Absyn.c

Buffer.o: Buffer.c Buffer.h
	$(CC) $(CFLAGS) -c Buffer.c

Lexer.o: Lexer.c Bison.h
	$(CC) $(CFLAGS) -Wno-sign-conversion -c Lexer.c

Parser.o: Parser.c Absyn.h Bison.h
	$(CC) $(CFLAGS) -c Parser.c

Printer.o: Printer.c Printer.h Absyn.h
	$(CC) $(CFLAGS) -c Printer.c

Test.o: Test.c Parser.h Printer.h Absyn.h
	$(CC) $(CFLAGS) -c Test.c

shell.o: shell.c shell.h Parser.h Absyn.h
	$(CC) $(CFLAGS) -c shell.c

# ── Binaries ─────────────────────────────────────────────────────────────────
# BNFC AST pretty-printer (useful for grammar debugging)
Test: $(OBJS) Test.o
	@echo "Linking Test..."
	$(CC) $(CFLAGS) $(OBJS) Test.o -o Test

# Mini-shell binary
shell: $(OBJS) shell.o
	@echo "Linking shell..."
	$(CC) $(CFLAGS) $(OBJS) shell.o -o shell

build: gen Test shell

# ── Convenience targets ───────────────────────────────────────────────────────
run: build
	@echo "echo hello | grep h > out.txt" | ./Test

ast: build
	@echo "ls -l | grep cf" | ./Test

test: build
	@bash ../tests/run_tests.sh

clean:
	@rm -f *.o *.tab.* lex.yy.c Test shell Parser.* Lexer.* \
	       Abs*.{c,h} Print*.{c,h} Test.c Skeleton.{c,h} Grammar.[ly] \
	       Grammar.l.bak Grammar.y.bak Bison.h out.txt
