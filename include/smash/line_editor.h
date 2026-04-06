#ifndef SMASH_LINE_EDITOR_H
#define SMASH_LINE_EDITOR_H

#include "state.h"

char *smash_read_line(SmashState *state, const char *prompt, int save_history);

#endif
