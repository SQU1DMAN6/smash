#include <stdio.h>
#include <string.h>

#include "smash/shell.h"

int main(int argc, char *argv[]) {
    SmashState state;
    int exit_code;
    const char *command = NULL;

    if (argc > 1 && strcmp(argv[1], "-c") == 0) {
        if (argc < 3) {
            fprintf(stderr, "smash: -c: expected a command\n");
            return 2;
        }
        command = argv[2];
    }

    smash_init(&state);
    
    if (command) {
        exit_code = smash_execute_line(&state, command, 0);
    } else {
        exit_code = smash_run(&state);
    }
    
    smash_cleanup(&state);
    return exit_code;
}
