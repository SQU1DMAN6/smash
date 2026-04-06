#include "smash/shell.h"

int main(void) {
    SmashState state;
    int exit_code;

    smash_init(&state);
    exit_code = smash_run(&state);
    smash_cleanup(&state);
    return exit_code;
}
