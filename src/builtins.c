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
        "Builtins:\n"
        "  Navigation: cd, pwd\n"
        "  Variables: set, export, unset, declare\n"
        "  Commands: type, which, alias\n"
        "  History: history\n"
        "  I/O: clear\n"
        "  Scripting: source (.)\n"
        "  Shell: exit, help\n";

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

static int run_set(SmashState *state, char **argv, int in_child) {
    (void) state;

    if (in_child) {
        fprintf(stderr, "smash: set: cannot be used in a pipeline\n");
        return 1;
    }

    if (!argv[1]) {
        extern char **environ;
        for (char **env = environ; *env; env++) {
            printf("%s\n", *env);
        }
        return 0;
    }

    char *name = NULL;
    const char *value = NULL;
    char *equals = strchr(argv[1], '=');

    if (equals) {
        size_t name_len = (size_t)(equals - argv[1]);
        if (name_len == 0) {
            fprintf(stderr, "smash: set: invalid variable name\n");
            return 1;
        }

        name = smash_xmalloc(name_len + 1);
        memcpy(name, argv[1], name_len);
        name[name_len] = '\0';
        value = equals + 1;
    } else if (argv[2]) {
        name = argv[1];
        value = argv[2];
    } else {
        fprintf(stderr, "smash: set: expected name=value or name value\n");
        return 1;
    }

    if (setenv(name, value, 1) != 0) {
        fprintf(stderr, "smash: set: failed to set %s: %s\n", name, strerror(errno));
        if (equals) {
            free(name);
        }
        return 1;
    }

    if (equals) {
        free(name);
    }
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

static int run_export(SmashState *state, char **argv, int in_child) {
    (void) state;

    if (in_child) {
        fprintf(stderr, "smash: export: cannot be used in a pipeline\n");
        return 1;
    }

    if (!argv[1]) {
        extern char **environ;
        for (char **env = environ; *env; env++) {
            printf("export %s\n", *env);
        }
        return 0;
    }

    for (int i = 1; argv[i]; i++) {
        char *equals = strchr(argv[i], '=');
        if (equals) {
            size_t name_len = (size_t)(equals - argv[i]);
            char *name = smash_xmalloc(name_len + 1);
            memcpy(name, argv[i], name_len);
            name[name_len] = '\0';
            setenv(name, equals + 1, 1);
            free(name);
        } else {
            const char *value = getenv(argv[i]);
            if (value) {
                setenv(argv[i], value, 1);
            }
        }
    }
    return 0;
}

static int run_unset(SmashState *state, char **argv, int in_child) {
    (void) state;

    if (in_child) {
        fprintf(stderr, "smash: unset: cannot be used in a pipeline\n");
        return 1;
    }

    if (!argv[1]) {
        fprintf(stderr, "smash: unset: expected at least one variable\n");
        return 1;
    }

    for (int i = 1; argv[i]; i++) {
        unsetenv(argv[i]);
    }
    return 0;
}

static int run_alias(SmashState *state, char **argv, int in_child) {
    if (in_child) {
        fprintf(stderr, "smash: alias: cannot be used in a pipeline\n");
        return 1;
    }

    if (!argv[1]) {
        for (size_t i = 0; i < state->alias_count; i++) {
            printf("alias %s='%s'\n", state->aliases[i].name, state->aliases[i].value);
        }
        return 0;
    }

    for (int i = 1; argv[i]; i++) {
        char *equals = strchr(argv[i], '=');
        if (!equals) {
            for (size_t j = 0; j < state->alias_count; j++) {
                if (strcmp(state->aliases[j].name, argv[i]) == 0) {
                    printf("alias %s='%s'\n", state->aliases[j].name, state->aliases[j].value);
                    return 0;
                }
            }
            fprintf(stderr, "smash: alias: %s not found\n", argv[i]);
            return 1;
        }

        size_t name_len = (size_t)(equals - argv[i]);
        char *name = smash_xmalloc(name_len + 1);
        memcpy(name, argv[i], name_len);
        name[name_len] = '\0';

        for (size_t j = 0; j < state->alias_count; j++) {
            if (strcmp(state->aliases[j].name, name) == 0) {
                free(state->aliases[j].value);
                state->aliases[j].value = smash_strdup(equals + 1);
                free(name);
                return 0;
            }
        }

        if (state->alias_count >= SMASH_ALIAS_LIMIT) {
            fprintf(stderr, "smash: alias limit exceeded\n");
            free(name);
            return 1;
        }

        state->aliases = smash_xrealloc(state->aliases, (state->alias_count + 1) * sizeof(SmashAlias));
        state->aliases[state->alias_count].name = name;
        state->aliases[state->alias_count].value = smash_strdup(equals + 1);
        state->alias_count++;
    }
    return 0;
}

static int run_type(SmashState *state, char **argv, int in_child) {
    (void) state;
    (void) in_child;

    if (!argv[1]) {
        fprintf(stderr, "smash: type: expected at least one command\n");
        return 1;
    }

    for (int i = 1; argv[i]; i++) {
        if (smash_is_builtin(argv[i])) {
            printf("%s is a shell builtin\n", argv[i]);
        } else if (smash_command_exists(argv[i])) {
            printf("%s is a command\n", argv[i]);
        } else {
            printf("%s not found\n", argv[i]);
        }
    }
    return 0;
}

static int run_declare(SmashState *state, char **argv, int in_child) {
    (void) state;

    if (in_child) {
        fprintf(stderr, "smash: declare: cannot be used in a pipeline\n");
        return 1;
    }

    for (int i = 1; argv[i]; i++) {
        char *equals = strchr(argv[i], '=');
        if (equals) {
            size_t name_len = (size_t)(equals - argv[i]);
            char *name = smash_xmalloc(name_len + 1);
            memcpy(name, argv[i], name_len);
            name[name_len] = '\0';
            setenv(name, equals + 1, 1);
            free(name);
        } else {
            setenv(argv[i], "", 1);
        }
    }
    return 0;
}

int smash_command_exists(const char *name) {
    if (!name || !*name) {
        return 0;
    }

    if (smash_is_builtin(name)) {
        return 1;
    }

    if (strchr(name, '/')) {
        return access(name, X_OK) == 0;
    }

    const char *path = getenv("PATH");
    if (!path) {
        return 0;
    }

    char *copy = smash_strdup(path);
    char *segment = strtok(copy, ":");
    int found = 0;

    while (segment) {
        size_t len = strlen(segment) + strlen(name) + 2;
        char *candidate = smash_xmalloc(len);

        snprintf(candidate, len, "%s/%s", segment, name);
        if (access(candidate, X_OK) == 0) {
            found = 1;
            free(candidate);
            break;
        }

        free(candidate);
        segment = strtok(NULL, ":");
    }

    free(copy);
    return found;
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
        strcmp(name, "set") == 0 ||
        strcmp(name, "export") == 0 ||
        strcmp(name, "unset") == 0 ||
        strcmp(name, "alias") == 0 ||
        strcmp(name, "declare") == 0 ||
        strcmp(name, ".") == 0) {
        return SMASH_BUILTIN_PARENT;
    }

    if (strcmp(name, "pwd") == 0 ||
        strcmp(name, "clear") == 0 ||
        strcmp(name, "help") == 0 ||
        strcmp(name, "which") == 0 ||
        strcmp(name, "type") == 0) {
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
    if (strcmp(argv[0], "set") == 0) {
        return run_set(state, argv, in_child);
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

    if (strcmp(argv[0], "export") == 0) {
        return run_export(state, argv, in_child);
    }

    if (strcmp(argv[0], "unset") == 0) {
        return run_unset(state, argv, in_child);
    }

    if (strcmp(argv[0], "alias") == 0) {
        return run_alias(state, argv, in_child);
    }

    if (strcmp(argv[0], "type") == 0) {
        return run_type(state, argv, in_child);
    }

    if (strcmp(argv[0], "declare") == 0) {
        return run_declare(state, argv, in_child);
    }

    return 1;
}