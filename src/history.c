#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "smash/history.h"
#include "smash/util.h"

void smash_history_add(SmashState *state, const char *line, int persist) {
    FILE *file;

    if (!line || line[0] == '\0') {
        return;
    }

    if (state->history_count == SMASH_HISTORY_LIMIT) {
        free(state->history[0]);
        memmove(
            state->history,
            state->history + 1,
            sizeof(state->history[0]) * (SMASH_HISTORY_LIMIT - 1)
        );
        state->history_count--;
    }

    state->history[state->history_count++] = smash_strdup(line);

    if (!persist || !state->history_path) {
        return;
    }

    file = fopen(state->history_path, "a");
    if (!file) {
        return;
    }

    fprintf(file, "%s\n", line);
    fclose(file);
}

void smash_history_load(SmashState *state) {
    FILE *file;
    char *line = NULL;
    size_t capacity = 0;
    ssize_t len;

    if (!state->history_path) {
        return;
    }

    file = fopen(state->history_path, "r");
    if (!file) {
        return;
    }

    while ((len = getline(&line, &capacity, file)) != -1) {
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }

        smash_history_add(state, line, 0);
    }

    free(line);
    fclose(file);
}

void smash_history_clear(SmashState *state) {
    FILE *file;
    size_t i;

    for (i = 0; i < state->history_count; i++) {
        free(state->history[i]);
        state->history[i] = NULL;
    }

    state->history_count = 0;

    if (!state->history_path) {
        return;
    }

    file = fopen(state->history_path, "w");
    if (file) {
        fclose(file);
    }
}

void smash_history_print(const SmashState *state) {
    size_t i;

    for (i = 0; i < state->history_count; i++) {
        printf("%4zu  %s\n", i + 1, state->history[i]);
    }
}

void smash_history_cleanup(SmashState *state) {
    size_t i;

    for (i = 0; i < state->history_count; i++) {
        free(state->history[i]);
        state->history[i] = NULL;
    }

    state->history_count = 0;
}
