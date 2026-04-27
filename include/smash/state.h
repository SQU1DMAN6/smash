#ifndef SMASH_STATE_H
#define SMASH_STATE_H

#include <stddef.h>
#include <termios.h>

#define SMASH_HISTORY_LIMIT 1024
#define SMASH_ALIAS_LIMIT 256

typedef struct {
    char *name;
    char *value;
} SmashAlias;

typedef struct {
    struct termios original_termios;
    int termios_initialized;
    int raw_mode_enabled;
    int exit_requested;
    int exit_code;
    char *history_path;
    char *config_path;
    char *history[SMASH_HISTORY_LIMIT];
    size_t history_count;
    SmashAlias *aliases;
    size_t alias_count;
} SmashState;

#endif
