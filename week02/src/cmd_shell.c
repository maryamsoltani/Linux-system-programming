#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../argtable3/argtable3.h"
#include "cmd_spec.h"

extern char **environ;

/* ════════════════════════════════════════════════════════════════════════════
 * pwd
 * ════════════════════════════════════════════════════════════════════════════ */

void pwd_print_usage(FILE *out) {
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output {\"cwd\":\"...\"}");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_j, opt_h, end };
    fprintf(out,"Usage: pwd"); arg_print_syntax(out,argtable,"\n");
    fprintf(out,"\n%s\n\nOptions:\n",spec_pwd.long_help);
    arg_print_glossary(out,argtable,"  %-22s %s\n");
    arg_free(argtable);
}

int pwd_run(int argc, char **argv) {
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output {\"cwd\":\"...\"}");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_j, opt_h, end };

    int nerrors = arg_parse(argc,argv,argtable);
    if (opt_h->count>0) { pwd_print_usage(stdout); arg_free(argtable); return 0; }
    if (nerrors>0) { arg_print_errors(stderr,end,"pwd"); arg_free(argtable); return 1; }

    char cwd[4096];
    if (!getcwd(cwd,sizeof(cwd))) { perror("pwd"); arg_free(argtable); return 1; }

    if (opt_j->count>0) printf("{\"cwd\":\"%s\"}\n", cwd);
    else                 printf("%s\n", cwd);

    arg_free(argtable); return 0;
}

cmd_spec_t spec_pwd = {
    .name="pwd", .summary="Print working directory",
    .long_help="Print the full path of the current working directory.",
    .category="shell", .run=pwd_run, .print_usage=pwd_print_usage,
};

/* ════════════════════════════════════════════════════════════════════════════
 * cd
 * ════════════════════════════════════════════════════════════════════════════ */

void cd_print_usage(FILE *out) {
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_d = arg_str0(NULL,NULL,"DIR","directory to change to (default: $HOME)");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_h, opt_d, end };
    fprintf(out,"Usage: cd"); arg_print_syntax(out,argtable,"\n");
    fprintf(out,"\n%s\n\nOptions:\n",spec_cd.long_help);
    arg_print_glossary(out,argtable,"  %-22s %s\n");
    arg_free(argtable);
}

int cd_run(int argc, char **argv) {
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_d = arg_str0(NULL,NULL,"DIR","directory to change to (default: $HOME)");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_h, opt_d, end };

    int nerrors = arg_parse(argc,argv,argtable);
    if (opt_h->count>0) { cd_print_usage(stdout); arg_free(argtable); return 0; }
    if (nerrors>0) { arg_print_errors(stderr,end,"cd"); arg_free(argtable); return 1; }

    const char *dir = (opt_d->count>0) ? opt_d->sval[0] : getenv("HOME");
    if (!dir) { fprintf(stderr,"cd: HOME not set\n"); arg_free(argtable); return 1; }
    if (chdir(dir)!=0) { perror(dir); arg_free(argtable); return 1; }

    arg_free(argtable); return 0;
}

cmd_spec_t spec_cd = {
    .name="cd", .summary="Change working directory",
    .long_help="Change the shell's current directory to DIR (default: $HOME).",
    .category="shell", .run=cd_run, .print_usage=cd_print_usage,
};

/* ════════════════════════════════════════════════════════════════════════════
 * env
 * ════════════════════════════════════════════════════════════════════════════ */

void env_print_usage(FILE *out) {
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output {\"env\":{\"KEY\":\"VALUE\",...}}");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_j, opt_h, end };
    fprintf(out,"Usage: env"); arg_print_syntax(out,argtable,"\n");
    fprintf(out,"\n%s\n\nOptions:\n",spec_env.long_help);
    arg_print_glossary(out,argtable,"  %-22s %s\n");
    arg_free(argtable);
}

int env_run(int argc, char **argv) {
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output {\"env\":{\"KEY\":\"VALUE\",...}}");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_j, opt_h, end };

    int nerrors = arg_parse(argc,argv,argtable);
    if (opt_h->count>0) { env_print_usage(stdout); arg_free(argtable); return 0; }
    if (nerrors>0) { arg_print_errors(stderr,end,"env"); arg_free(argtable); return 1; }

    int json = opt_j->count>0;
    if (json) printf("{\"env\":{");

    int first = 1;
    for (char **e = environ; *e; e++) {
        char *eq = strchr(*e,'=');
        if (!eq) continue;
        if (json) {
            char key[256]; int klen = (int)(eq - *e);
            if (klen>=(int)sizeof(key)) klen=(int)sizeof(key)-1;
            strncpy(key,*e,klen); key[klen]='\0';
            if (!first) printf(",");
            printf("\"%s\":\"%s\"", key, eq+1);
            first = 0;
        } else printf("%s\n", *e);
    }

    if (json) printf("}}\n");
    arg_free(argtable); return 0;
}

cmd_spec_t spec_env = {
    .name="env", .summary="List environment variables",
    .long_help="Print all environment variables as KEY=VALUE pairs.",
    .category="shell", .run=env_run, .print_usage=env_print_usage,
};

/* ════════════════════════════════════════════════════════════════════════════
 * export
 * ════════════════════════════════════════════════════════════════════════════ */

void export_print_usage(FILE *out) {
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output JSON status");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_k = arg_strn(NULL,NULL,"KEY=VALUE",1,100,"variable(s) to set");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_j, opt_h, opt_k, end };
    fprintf(out,"Usage: export"); arg_print_syntax(out,argtable,"\n");
    fprintf(out,"\n%s\n\nOptions:\n",spec_export.long_help);
    arg_print_glossary(out,argtable,"  %-22s %s\n");
    arg_free(argtable);
}

int export_run(int argc, char **argv) {
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output JSON status");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_k = arg_strn(NULL,NULL,"KEY=VALUE",1,100,"variable(s) to set");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_j, opt_h, opt_k, end };

    int nerrors = arg_parse(argc,argv,argtable);
    if (opt_h->count>0) { export_print_usage(stdout); arg_free(argtable); return 0; }
    if (nerrors>0) { arg_print_errors(stderr,end,"export"); arg_free(argtable); return 1; }

    int json=opt_j->count>0, ret=0;
    for (int i=0; i<opt_k->count; i++) {
        const char *kv = opt_k->sval[i];
        char *eq = strchr(kv,'=');
        int r = 0;
        if (!eq) { fprintf(stderr,"export: invalid format (use KEY=VALUE): %s\n",kv); r=1; }
        else {
            char key[256]; int klen=(int)(eq-kv);
            strncpy(key,kv,klen); key[klen]='\0';
            if (setenv(key,eq+1,1)!=0) { perror("export"); r=1; }
        }
        if (json) printf("{\"expr\":\"%s\",\"status\":\"%s\"}\n",kv,r==0?"ok":"error");
        ret |= r;
    }
    arg_free(argtable); return ret;
}

cmd_spec_t spec_export = {
    .name="export", .summary="Set environment variable",
    .long_help="Set an environment variable using KEY=VALUE syntax.",
    .category="shell", .run=export_run, .print_usage=export_print_usage,
};

/* ════════════════════════════════════════════════════════════════════════════
 * unset
 * ════════════════════════════════════════════════════════════════════════════ */

void unset_print_usage(FILE *out) {
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output JSON status");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_k = arg_strn(NULL,NULL,"KEY",1,100,"variable name(s) to unset");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_j, opt_h, opt_k, end };
    fprintf(out,"Usage: unset"); arg_print_syntax(out,argtable,"\n");
    fprintf(out,"\n%s\n\nOptions:\n",spec_unset.long_help);
    arg_print_glossary(out,argtable,"  %-22s %s\n");
    arg_free(argtable);
}

int unset_run(int argc, char **argv) {
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output JSON status");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_k = arg_strn(NULL,NULL,"KEY",1,100,"variable name(s) to unset");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_j, opt_h, opt_k, end };

    int nerrors = arg_parse(argc,argv,argtable);
    if (opt_h->count>0) { unset_print_usage(stdout); arg_free(argtable); return 0; }
    if (nerrors>0) { arg_print_errors(stderr,end,"unset"); arg_free(argtable); return 1; }

    int json=opt_j->count>0, ret=0;
    for (int i=0; i<opt_k->count; i++) {
        const char *key = opt_k->sval[i];
        unsetenv(key);
        if (json) printf("{\"key\":\"%s\",\"status\":\"ok\"}\n", key);
    }
    arg_free(argtable); return ret;
}

cmd_spec_t spec_unset = {
    .name="unset", .summary="Unset environment variable",
    .long_help="Remove KEY from the environment.",
    .category="shell", .run=unset_run, .print_usage=unset_print_usage,
};

/* ════════════════════════════════════════════════════════════════════════════
 * type
 * ════════════════════════════════════════════════════════════════════════════ */

void type_print_usage(FILE *out) {
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output JSON {name, kind, path}");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_n = arg_strn(NULL,NULL,"NAME",1,100,"command name(s) to look up");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_j, opt_h, opt_n, end };
    fprintf(out,"Usage: type"); arg_print_syntax(out,argtable,"\n");
    fprintf(out,"\n%s\n\nOptions:\n",spec_type.long_help);
    arg_print_glossary(out,argtable,"  %-22s %s\n");
    arg_free(argtable);
}

int type_run(int argc, char **argv) {
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output JSON {name, kind, path}");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_n = arg_strn(NULL,NULL,"NAME",1,100,"command name(s) to look up");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_j, opt_h, opt_n, end };

    int nerrors = arg_parse(argc,argv,argtable);
    if (opt_h->count>0) { type_print_usage(stdout); arg_free(argtable); return 0; }
    if (nerrors>0) { arg_print_errors(stderr,end,"type"); arg_free(argtable); return 1; }

    int json=opt_j->count>0, ret=0;

    for (int i=0; i<opt_n->count; i++) {
        const char *name = opt_n->sval[i];

        /* Check registry (builtin) */
        int is_builtin = 0;
        for (cmd_spec_t * const *s = COMMAND_REGISTRY; *s; s++) {
            if (strcmp((*s)->name, name)==0) { is_builtin=1; break; }
        }

        if (is_builtin) {
            if (json) printf("{\"name\":\"%s\",\"kind\":\"builtin\",\"path\":null}\n",name);
            else      printf("%s is a shell builtin\n",name);
            continue;
        }

        /* Search PATH for external */
        char *path_env = getenv("PATH");
        char found[4096] = "";
        if (path_env) {
            char *path_copy = strdup(path_env);
            char *tok = strtok(path_copy,":");
            while (tok) {
                char candidate[4096];
                snprintf(candidate,sizeof(candidate),"%s/%s",tok,name);
                if (access(candidate,X_OK)==0) { strncpy(found,candidate,sizeof(found)-1); break; }
                tok = strtok(NULL,":");
            }
            free(path_copy);
        }

        if (found[0]) {
            if (json) printf("{\"name\":\"%s\",\"kind\":\"external\",\"path\":\"%s\"}\n",name,found);
            else      printf("%s is %s\n",name,found);
        } else {
            if (json) printf("{\"name\":\"%s\",\"kind\":\"not-found\",\"path\":null}\n",name);
            else      fprintf(stderr,"%s: not found\n",name);
            ret = 1;
        }
    }
    arg_free(argtable); return ret;
}

cmd_spec_t spec_type = {
    .name="type", .summary="Show how a command name is resolved",
    .long_help="Report whether NAME is a builtin or external command, and its path.",
    .category="shell", .run=type_run, .print_usage=type_print_usage,
};
