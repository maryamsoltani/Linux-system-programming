# Shell Coding Reference (Week 07)

A compact reference for the mini-shell execution layer.

---

## 1. Grammar entrypoint

```c
#include "Parser.h"
Input tree = psInput(line);   /* parse a C string */
Input tree = pInput(fp);      /* parse a FILE*    */
```

Both return `NULL` on parse failure.

---

## 2. AST hierarchy

```
Input
  StartInput -> ListJob
                  Job (OneJobFG | OneJobBG)
                    CommandLine (MkCmdLine)
                      Pipeline (Single | Pipe)
                        CommandPart (Cmd)
                          Atom (AWord | AVar | AQuoted)
                      OptRedir (NoRedir | OutRedir | InRedir |
                                InOutRedir | OutInRedir)
```

---

## 3. Atom expansion

```c
static char *expand_atom(Atom a) {
    switch (a->kind) {
        case is_AWord:   return strdup(a->u.aword_.word_);
        case is_AVar:    { const char *v = getenv(a->u.avar_.varref_ + 1);
                           return strdup(v ? v : ""); }
        case is_AQuoted: return strdup(a->u.aquoted_.string_);
    }
}
```

`VarRef` tokens include the leading `$`; skip it with `+ 1`.

---

## 4. Pipeline execution (n commands)

```c
int pipes[n-1][2];
for (int i = 0; i < n-1; i++) pipe(pipes[i]);

for (int i = 0; i < n; i++) {
    if (fork() == 0) {
        if (i > 0) dup2(pipes[i-1][0], STDIN_FILENO);
        if (i < n-1) dup2(pipes[i][1],  STDOUT_FILENO);
        /* close all pipe fds */
        execvp(cmd[i].name, cmd[i].argv);
        _exit(127);
    }
}
/* parent: close all pipe fds, then waitpid for each child */
```

---

## 5. I/O redirection

```c
/* output redirect */
int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
dup2(fd, STDOUT_FILENO);
close(fd);

/* input redirect */
int fd = open(path, O_RDONLY);
dup2(fd, STDIN_FILENO);
close(fd);
```

---

## 6. Variable assignment

Detect `name=value` tokens:

```c
char *eq = strchr(word, '=');
if (eq && eq != word) {
    char *name = strndup(word, eq - word);
    setenv(name, eq + 1, /*overwrite=*/1);
    free(name);
}
```

---

## 7. Built-ins

| Command  | Implemented in | Notes |
|----------|---------------|-------|
| `cd`     | `execute_command` | `chdir(dir)` |
| `exit`   | `execute_command` | calls `exit(code)` |
| `export` | `execute_command` | `setenv(name, val, 1)` |

---

## 8. Background jobs

```c
pid_t pid = fork();
if (pid == 0) { execvp(...); _exit(127); }
if (!background) waitpid(pid, NULL, 0);
/* if background==1, we do NOT wait — process runs detached */
```

---

## 9. Connecting command modules (future)

```c
/* In shell.c */
extern void register_hello_command(void);

void register_all_builtin_commands(void) {
    register_hello_command();
    /* ... */
}

void execute_command(const Command *cmd) {
    const cmd_spec_t *spec = find_command(cmd->name);
    if (spec) { spec->run(cmd->argc, cmd->argv); return; }
    /* fallback to fork/execvp */
}
```
