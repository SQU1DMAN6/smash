#ifndef SMASH_TERM_H
#define SMASH_TERM_H

#include "state.h"

void smash_term_enable_raw(SmashState *state);
void smash_term_disable_raw(SmashState *state);
int smash_term_columns(void);

#endif
