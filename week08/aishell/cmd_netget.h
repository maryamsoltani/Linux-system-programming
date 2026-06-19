#ifndef CMD_NETGET_H
#define CMD_NETGET_H

#include <stdio.h>

int  netget_run(int argc, char **argv);
void netget_print_usage(FILE *out);
void register_netget_command(void);

#endif
