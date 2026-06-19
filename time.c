#include <stdio.h>
#include <string.h>
#include <time.h>

void print_help() {
    printf("Usage: ./mytool <command>\n");
    printf("\n");
    printf("Commands:\n");
    printf("  date      Print today's date\n");
    printf("  time      Print current time\n");
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help   Show this help message\n");
}

void cmd_date() {
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char buffer[20];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d", tm_info);
    printf("Today's Date: %s\n", buffer);
}

void cmd_time() {
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char buffer[20];
    strftime(buffer, sizeof(buffer), "%H:%M:%S", tm_info);
    printf("Current Time: %s\n", buffer);
}

int main(int argc, char *argv[]) {

    // Error: no command given
    if (argc < 2) {
        fprintf(stderr, "Error: No command given. Use -h for help.\n");
        return 1;
    }

    // Check what command was typed
    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_help();

    } else if (strcmp(argv[1], "date") == 0) {
        cmd_date();

    } else if (strcmp(argv[1], "time") == 0) {
        cmd_time();

    } else {
        // Error: unknown command
        fprintf(stderr, "Error: Unknown command '%s'. Use -h for help.\n", argv[1]);
        return 1;
    }

    return 0;
}