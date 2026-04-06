#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "smash/executor.h"
#include "smash/history.h"
#include "smash/line_editor.h"
#include "smash/parser.h"
#include "smash/shell.h"
#include "smash/term.h"
#include "smash/util.h"

#define PROMPT "=> "

static void ignore_shell_signals(void) {
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
}

static void sync_shell_environment(void) {
    char cwd[4096];

    if (getcwd(cwd, sizeof(cwd))) {
        setenv("PWD", cwd, 1);
    }

    setenv("SMASH_SHELL", "1", 1);
}

static char *read_heredoc_line(void *context, const char *prompt, int save_history) {
    return smash_read_line((SmashState *) context, prompt, save_history);
}

int smash_execute_line(SmashState *state, const char *line, int interactive) {
    SmashPipeline pipeline;
    char *working = smash_strdup(line);
    char *trimmed = smash_trim_in_place(working);
    char *error_message = NULL;
    int status = 0;

    if (*trimmed == '\0') {
        free(working);
        return 0;
    }

    if (!smash_parse_line(
            trimmed,
            interactive ? read_heredoc_line : NULL,
            state,
            &pipeline,
            &error_message)) {
        fprintf(stderr, "smash: %s\n", error_message ? error_message : "parse error");
        free(error_message);
        free(working);
        return 1;
    }

    status = smash_execute_pipeline(state, &pipeline);
    smash_destroy_pipeline(&pipeline);
    free(error_message);
    free(working);
    return status;
}

int smash_execute_script_file(SmashState *state, const char *path) {
    FILE *file = fopen(path, "r");
    char *line = NULL;
    size_t capacity = 0;
    ssize_t len;

    if (!file) {
        perror("smash: source");
        return 1;
    }

    while ((len = getline(&line, &capacity, file)) != -1) {
        char *trimmed;

        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }

        trimmed = smash_trim_in_place(line);
        if (*trimmed == '\0' || *trimmed == '#') {
            continue;
        }

        smash_execute_line(state, trimmed, 0);
        if (state->exit_requested) {
            break;
        }
    }

    free(line);
    fclose(file);
    return 0;
}

int smash_init(SmashState *state) {
    memset(state, 0, sizeof(*state));

    state->history_path = smash_build_home_path(".smash_history");
    state->config_path = smash_build_home_path(".smashrc");

    ignore_shell_signals();
    sync_shell_environment();
    smash_history_load(state);

    if (state->config_path && access(state->config_path, F_OK) == 0) {
        smash_execute_script_file(state, state->config_path);
    }

    return 0;
}

void smash_cleanup(SmashState *state) {
    smash_term_disable_raw(state);
    smash_history_cleanup(state);
    free(state->history_path);
    free(state->config_path);
    state->history_path = NULL;
    state->config_path = NULL;
}

int smash_run(SmashState *state) {
    printf("SMASH 0.1.0, written by Quan Thai\n");
    smash_term_enable_raw(state);

    while (!state->exit_requested) {
        char *line = smash_read_line(state, PROMPT, 1);

        if (!line) {
            state->exit_requested = 1;
            state->exit_code = 0;
            break;
        }

        smash_execute_line(state, line, 1);
        free(line);
    }

    return state->exit_code;
}
