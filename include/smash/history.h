#ifndef SMASH_HISTORY_H
#define SMASH_HISTORY_H

#include "state.h"

void smash_history_load(SmashState *state);
void smash_history_add(SmashState *state, const char *line, int persist);
void smash_history_clear(SmashState *state);
void smash_history_print(const SmashState *state);
void smash_history_cleanup(SmashState *state);

#endif
