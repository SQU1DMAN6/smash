#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "smash/builtins.h"
#include "smash/history.h"
#include "smash/shell.h"
#include "smash/util.h"

static int print_help(void) {
    static const char *message =
        "Builtins: cd, clear, exit, help, history, pwd, source, which\n";

    write(STDOUT_FILENO, message, strlen(message));
    return 0;
}

static int run_cd(SmashState *state, char **argv, int in_child) {
    char previous_dir[PATH_MAX];
    char current_dir[PATH_MAX];
    char *expanded;
    const char *target;

    (void) state;

    if (in_child) {
        fprintf(stderr, "smash: cd: cannot be used in a pipeline\n");
        return 1;
    }

    if (!getcwd(previous_dir, sizeof(previous_dir))) {
        previous_dir[0] = '\0';
    }

    if (!argv[1]) {
        target = getenv("HOME");
    } else if (strcmp(argv[1], "-") == 0) {
        target = getenv("OLDPWD");
    } else {
        target = argv[1];
    }

    if (!target || target[0] == '\0') {
        fprintf(stderr, "smash: cd: target directory not set\n");
        return 1;
    }

    expanded = smash_expand_path(target);
    if (chdir(expanded) != 0) {
        fprintf(stderr, "smash: cd: %s: %s\n", expanded, strerror(errno));
        free(expanded);
        return 1;
    }

    if (previous_dir[0] != '\0') {
        setenv("OLDPWD", previous_dir, 1);
    }

    if (getcwd(current_dir, sizeof(current_dir))) {
        setenv("PWD", current_dir, 1);
    }

    free(expanded);
    return 0;
}

static int run_pwd(void) {
    char cwd[PATH_MAX];

    if (!getcwd(cwd, sizeof(cwd))) {
        fprintf(stderr, "smash: pwd: %s\n", strerror(errno));
        return 1;
    }

    printf("%s\n", cwd);
    return 0;
}

static int run_clear(void) {
    const char *CLEAR_SCREEN_ANSI = "\033[H\033[2J";
    write(STDOUT_FILENO, CLEAR_SCREEN_ANSI, strlen(CLEAR_SCREEN_ANSI));
    return 0;
}

static int run_history(SmashState *state, char **argv, int in_child) {
    if (argv[1] && strcmp(argv[1], "-c") == 0) {
        if (in_child) {
            fprintf(stderr, "smash: history -c: cannot be used in a pipeline\n");
            return 1;
        }

        smash_history_clear(state);
        return 0;
    }

    smash_history_print(state);
    return 0;
}

static int run_exit(SmashState *state, char **argv, int in_child) {
    long code = 0;
    char *end = NULL;

    if (in_child) {
        fprintf(stderr, "smash: exit: cannot be used in a pipeline\n");
        return 1;
    }

    if (!argv[1]) {
        state->exit_requested = 1;
        state->exit_code = 0;
        return 0;
    }

    errno = 0;
    code = strtol(argv[1], &end, 10);
    if (errno != 0 || !end || *end != '\0') {
        fprintf(stderr, "smash: exit: numeric argument required\n");
        state->exit_requested = 1;
        state->exit_code = 2;
        return 2;
    }

    state->exit_requested = 1;
    state->exit_code = (int) (code & 0xff);
    return 0;
}

static int run_source(SmashState *state, char **argv, int in_child) {
    char *expanded;
    int status;

    if (in_child) {
        fprintf(stderr, "smash: source: cannot be used in a pipeline\n");
        return 1;
    }

    if (!argv[1]) {
        fprintf(stderr, "smash: source: expected a file path\n");
        return 1;
    }

    expanded = smash_expand_path(argv[1]);
    status = smash_execute_script_file(state, expanded);
    free(expanded);
    return status;
}

static int run_which(char **argv) {
    int status = 0;
    int i;

    if (!argv[1]) {
        fprintf(stderr, "smash: which: expected at least one command\n");
        return 1;
    }

    for (i = 1; argv[i]; i++) {
        char *name = argv[i];

        if (smash_is_builtin(name)) {
            printf("%s: shell builtin\n", name);
            continue;
        }

        if (strchr(name, '/')) {
            if (access(name, X_OK) == 0) {
                printf("%s\n", name);
                continue;
            }
        } else {
            char *path = getenv("PATH");
            char *copy;
            char *segment;
            int found = 0;

            if (access(name, X_OK) == 0) {
                char cwd[PATH_MAX];

                if (getcwd(cwd, sizeof(cwd))) {
                    printf("%s/%s\n", cwd, name);
                    continue;
                }
            }

            if (!path) {
                fprintf(stderr, "smash: which: %s not found\n", name);
                status = 1;
                continue;
            }

            copy = smash_strdup(path);
            segment = strtok(copy, ":");
            while (segment) {
                size_t len = strlen(segment) + strlen(name) + 2;
                char *candidate = smash_xmalloc(len);

                snprintf(candidate, len, "%s/%s", segment, name);
                if (access(candidate, X_OK) == 0) {
                    printf("%s\n", candidate);
                    found = 1;
                    free(candidate);
                    break;
                }

                free(candidate);
                segment = strtok(NULL, ":");
            }

            free(copy);

            if (!found) {
                fprintf(stderr, "smash: which: %s not found\n", name);
                status = 1;
            }
        }
    }

    return status;
}

int smash_is_builtin(const char *name) {
    return smash_builtin_scope(name) != SMASH_BUILTIN_NONE;
}

SmashBuiltinScope smash_builtin_scope(const char *name) {
    if (!name) {
        return SMASH_BUILTIN_NONE;
    }

    if (strcmp(name, "cd") == 0 ||
        strcmp(name, "exit") == 0 ||
        strcmp(name, "history") == 0 ||
        strcmp(name, "source") == 0 ||
        strcmp(name, ".") == 0) {
        return SMASH_BUILTIN_PARENT;
    }

    if (strcmp(name, "pwd") == 0 ||
        strcmp(name, "clear") == 0 ||
        strcmp(name, "help") == 0 ||
        strcmp(name, "which") == 0) {
        return SMASH_BUILTIN_CHILD;
    }

    return SMASH_BUILTIN_NONE;
}

int smash_run_builtin(SmashState *state, char **argv, int in_child) {
    if (!argv || !argv[0]) {
        return 0;
    }

    if (strcmp(argv[0], "cd") == 0) {
        return run_cd(state, argv, in_child);
    }

    if (strcmp(argv[0], "pwd") == 0) {
        return run_pwd();
    }

    if (strcmp(argv[0], "clear") == 0) {
        return run_clear();
    }

    if (strcmp(argv[0], "help") == 0) {
        return print_help();
    }

    if (strcmp(argv[0], "history") == 0) {
        return run_history(state, argv, in_child);
    }

    if (strcmp(argv[0], "exit") == 0) {
        return run_exit(state, argv, in_child);
    }

    if (strcmp(argv[0], "source") == 0 || strcmp(argv[0], ".") == 0) {
        return run_source(state, argv, in_child);
    }

    if (strcmp(argv[0], "which") == 0) {
        return run_which(argv);
    }

    return 1;
}