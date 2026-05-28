# BNF Shell Lab Handout (Session 7)

## Overview

In this lab you build a **grammar-based mini-shell** using BNFC (BNF Converter).
The key idea from the slides: execution is a *transformation*:

```
Input string -> Lexer -> Tokens -> Parser -> AST -> Evaluator -> fork/exec
```

BNFC generates the lexer, parser, and AST types from a single grammar file
(`Grammar.cf`). You write the evaluator (`shell.c`) that walks the AST.

---

## 1. Grammar at a glance

The grammar lives in `bncf/Grammar.cf`. Key rules:

| Rule | Meaning |
|------|---------|
| `StartInput. Input ::= [Job] ;` | An input line is a list of jobs |
| `separator nonempty Job ";" ;` | Jobs are separated by `;` |
| `OneJobFG. Job ::= CommandLine ;` | A foreground job |
| `OneJobBG. Job ::= CommandLine "&" ;` | A background job |
| `Pipe. Pipeline ::= CommandPart "\|" Pipeline ;` | Pipeline of commands |
| `OutRedir. OptRedir ::= ">" Atom ;` | Output redirection |
| `InRedir.  OptRedir ::= "<" Atom ;` | Input redirection |
| `AVar. Atom ::= VarRef ;` | `$NAME` variable reference |

The `Word` token covers command names, paths, flags, and `NAME=value` strings.
The `VarRef` token matches `$NAME`.

---

## 2. Build steps

```bash
cd bncf/
make          # runs bnfc, flex, bison, gcc -- produces Test and shell
make test     # runs the automated test suite (25 tests)
```

What `make` does internally:
1. `bnfc --c Grammar.cf` -- generates `Absyn.*`, `Grammar.l`, `Grammar.y`, `Printer.*`, `Test.c`
2. Two idempotent sed patches fix the reentrant flex/bison integration
3. `flex Grammar.l` -> `Lexer.c`
4. `bison Grammar.y` -> `Parser.c`
5. `gcc` compiles everything -> `Test` and `shell`

---

## 3. Debugging with the AST printer

Before running the shell, check that your grammar parses correctly:

```bash
echo "ls -l | grep .c"          | ./Test
echo "sort < input.txt > out"   | ./Test
echo 'MY_VAR=hello ; echo $MY_VAR' | ./Test
echo "sleep 3 &"                | ./Test
echo "echo a ; echo b ; echo c" | ./Test
```

Each should print `Parse Successful!` followed by the AST tree.

If you get a parse error, the grammar or token definition needs fixing.

---

## 4. Shell execution flow

`shell.c` implements the REPL and the AST evaluator:

```
main()
  |
  +-- getline(stdin)          -- read one line
  +-- psInput(line)           -- parse -> Input AST (or NULL on error)
  +-- eval_input(tree)
        |
        +-- for each Job in ListJob:
              OneJobFG -> eval_commandline(cl, background=0)
              OneJobBG -> eval_commandline(cl, background=1)
                |
                +-- pipeline_depth(pipeline)
                |     == 1: execute_command(&cmd)
                |     > 1:  execute_pipeline(cmds[], n, background)
                |
                +-- try_assignment(word)  -- detects NAME=value
                +-- apply_redir(redir)    -- sets infile/outfile
```

---

## 5. Variable assignment and expansion

**Assignment** (`NAME=value`): detected in `try_assignment()` when a
single-atom command matches the pattern `[A-Za-z_][A-Za-z0-9_]*=.*`.
Uses `setenv(name, value, 1)`.

**Expansion** (`$NAME`): done in `expand_atom()` when an `AVar` atom
is encountered. Uses `getenv(name+1)` (skipping the `$`).

Example session:
```
mini-shell> NAME=Alice
mini-shell> echo Hello $NAME
Hello Alice
mini-shell> export PATH=/usr/local/bin:$PATH
```

---

## 6. Pipelines

`execute_pipeline(cmds, n, background)` in `shell.c`:

1. Creates `n-1` pipes with `pipe()`.
2. Forks `n` children.
3. Each child wires its stdin from `pipes[i-1][0]` and stdout to `pipes[i][1]`
   using `dup2()`, closes all pipe fds, then calls `execvp()`.
4. Parent closes all pipe fds, then `waitpid()` for each child
   (unless `background==1`).

---

## 7. Week 7 acceptance checklist

- [ ] `make` succeeds without errors
- [ ] `./Test` prints correct ASTs for: simple command, pipeline, redirection, background, variable
- [ ] Parser prints a useful error for invalid syntax (e.g. `| ls`)
- [ ] Pipeline executes correctly: `ls | grep .c | wc -l`
- [ ] Output redirection: `echo hello > /tmp/out.txt ; cat /tmp/out.txt`
- [ ] Input redirection: `wc -l < /etc/hostname`
- [ ] Variable assignment + expansion: `X=hello ; echo $X`
- [ ] Background job: `sleep 1 &` returns prompt immediately
- [ ] Built-ins work: `cd /tmp`, `export X=1`, `exit`
- [ ] `make test` passes all 25 tests

---

## 8. Extension ideas (optional)

From the slides:

- **Wildcard expansion (globbing)**: convert `*.c` to a regex, use
  `regcomp`/`regexec` on directory entries via `readdir`.
- **Special variables**: `$?` (last exit status), `$$` (PID), `$#` (arg count).
- **`if/then/fi`**: add grammar rules and an evaluator branch.
- **Subcommand substitution**: `` `cmd` `` runs a command and substitutes its output.
- **`@`-prefixed AI queries**: pipe the query to `mysh_llm`, get a shell
  command back, confirm and execute.

---

## 9. AI work log template

Keep a log of 5-10 key prompts you used and which diffs you accepted:

| # | Prompt | Accepted change |
|---|--------|----------------|
| 1 | "Given Grammar.cf, list all tokens and nonterminals" | understood AST structure |
| 2 | "Add VarRef token for $NAME expansion" | added `token VarRef` rule |
| 3 | "Explain shift/reduce conflict in pipeline rule" | fixed grammar ordering |
| 4 | ... | ... |
