#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
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

static char *evaluate_ps1(const char *ps1_template) {
    if (!ps1_template) {
        return smash_strdup(PROMPT);
    }

    char *result = smash_strdup("");
    size_t result_len = 0;
    size_t result_cap = 1;

    for (const char *p = ps1_template; *p; p++) {
        if (*p == '$' && *(p + 1) == '(') {
            size_t cmd_len = 0;
            const char *cmd_start = p + 2;
            const char *cmd_end = cmd_start;
            int depth = 1;

            while (*cmd_end && depth > 0) {
                if (*cmd_end == '(') depth++;
                if (*cmd_end == ')') depth--;
                if (depth > 0) cmd_end++;
            }

            if (depth == 0) {
                cmd_len = (size_t)(cmd_end - cmd_start);
                char *cmd = smash_xmalloc(cmd_len + 1);
                memcpy(cmd, cmd_start, cmd_len);
                cmd[cmd_len] = '\0';

                FILE *pipe = popen(cmd, "r");
                if (pipe) {
                    char buf[256];
                    while (fgets(buf, sizeof(buf), pipe)) {
                        for (char *q = buf; *q; q++) {
                            if (*q == '\n' || *q == '\r') {
                                *q = '\0';
                                break;
                            }
                        }
                        size_t buf_len = strlen(buf);
                        if (result_len + buf_len + 1 > result_cap) {
                            result_cap = (result_len + buf_len + 1) * 2;
                            result = smash_xrealloc(result, result_cap);
                        }
                        strcpy(result + result_len, buf);
                        result_len += buf_len;
                    }
                    pclose(pipe);
                }
                free(cmd);
                p = cmd_end;
                continue;
            }
        } else if (*p == '$' && (isalpha((unsigned char)*(p + 1)) || *(p + 1) == '_')) {
            size_t var_len = 0;
            const char *var_start = p + 1;
            const char *var_end = var_start;
            while (*var_end && (isalnum((unsigned char)*var_end) || *var_end == '_')) {
                var_end++;
            }
            var_len = (size_t)(var_end - var_start);
            char *var_name = smash_xmalloc(var_len + 1);
            memcpy(var_name, var_start, var_len);
            var_name[var_len] = '\0';

            const char *var_value = getenv(var_name);
            if (var_value) {
                size_t val_len = strlen(var_value);
                if (result_len + val_len + 1 > result_cap) {
                    result_cap = (result_len + val_len + 1) * 2;
                    result = smash_xrealloc(result, result_cap);
                }
                strcpy(result + result_len, var_value);
                result_len += val_len;
            }
            free(var_name);
            p = var_end - 1;
            continue;
        } else if (*p == '\\' && *(p + 1)) {
            p++;
            if (*p == 'n') {
                if (result_len + 2 > result_cap) {
                    result_cap = (result_len + 2) * 2;
                    result = smash_xrealloc(result, result_cap);
                }
                result[result_len++] = '\n';
                result[result_len] = '\0';
            } else if (*p == 'r') {
                if (result_len + 2 > result_cap) {
                    result_cap = (result_len + 2) * 2;
                    result = smash_xrealloc(result, result_cap);
                }
                result[result_len++] = '\r';
                result[result_len] = '\0';
            } else {
                if (result_len + 2 > result_cap) {
                    result_cap = (result_len + 2) * 2;
                    result = smash_xrealloc(result, result_cap);
                }
                result[result_len++] = *p;
                result[result_len] = '\0';
            }
            continue;
        }

        if (result_len + 2 > result_cap) {
            result_cap = (result_len + 2) * 2;
            result = smash_xrealloc(result, result_cap);
        }
        result[result_len++] = *p;
        result[result_len] = '\0';
    }

    return result;
}

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

    if (*trimmed == '\0' || *trimmed == '#') {
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
    
    setenv("PS1", "$(pwd) => ", 0);
    
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
    
    for (size_t i = 0; i < state->alias_count; i++) {
        free(state->aliases[i].name);
        free(state->aliases[i].value);
    }
    free(state->aliases);
    state->aliases = NULL;
    state->alias_count = 0;
}

int smash_run(SmashState *state) {
    printf("SMASH 1.0.0, written by Quan Thai\n");
    smash_term_enable_raw(state);

    while (!state->exit_requested) {
        const char *ps1_template = getenv("PS1");
        char *prompt = evaluate_ps1(ps1_template);
        char *line = smash_read_line(state, prompt, 1);
        free(prompt);

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
