#ifndef SMASH_SHELL_H
#define SMASH_SHELL_H

#include "state.h"

int smash_init(SmashState *state);
void smash_cleanup(SmashState *state);
int smash_run(SmashState *state);
int smash_execute_line(SmashState *state, const char *line, int interactive);
int smash_execute_script_file(SmashState *state, const char *path);

#endif
