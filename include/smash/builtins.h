#ifndef SMASH_BUILTINS_H
#define SMASH_BUILTINS_H

#include "state.h"

typedef enum {
    SMASH_BUILTIN_NONE = 0,
    SMASH_BUILTIN_PARENT,
    SMASH_BUILTIN_CHILD
} SmashBuiltinScope;

SmashBuiltinScope smash_builtin_scope(const char *name);
int smash_run_builtin(SmashState *state, char **argv, int in_child);
int smash_is_builtin(const char *name);

#endif
