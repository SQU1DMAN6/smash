#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "smash/builtins.h"
#include "smash/executor.h"
#include "smash/term.h"

static void restore_default_signals(void) {
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
}

static int apply_redirections(const SmashCommand *command) {
    size_t i;

    for (i = 0; i < command->redirection_count; i++) {
        const SmashRedirection *redirection = &command->redirections[i];
        int fd = -1;

        if (redirection->type == SMASH_REDIR_HEREDOC) {
            int pipe_fd[2];

            if (pipe(pipe_fd) != 0) {
                fprintf(stderr, "smash: pipe failed for heredoc: %s\n", strerror(errno));
                return 1;
            }

            if (redirection->data) {
                write(pipe_fd[1], redirection->data, strlen(redirection->data));
            }

            close(pipe_fd[1]);
            if (dup2(pipe_fd[0], STDIN_FILENO) == -1) {
                close(pipe_fd[0]);
                return 1;
            }
            close(pipe_fd[0]);
            continue;
        }

        if (redirection->type == SMASH_REDIR_INPUT) {
            fd = open(redirection->target, O_RDONLY);
            if (fd == -1) {
                fprintf(stderr, "smash: cannot open %s: %s\n", redirection->target, strerror(errno));
                return 1;
            }

            if (dup2(fd, STDIN_FILENO) == -1) {
                close(fd);
                return 1;
            }
        } else if (redirection->type == SMASH_REDIR_OUTPUT) {
            fd = open(redirection->target, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd == -1) {
                fprintf(stderr, "smash: cannot open %s: %s\n", redirection->target, strerror(errno));
                return 1;
            }

            if (dup2(fd, STDOUT_FILENO) == -1) {
                close(fd);
                return 1;
            }
        } else if (redirection->type == SMASH_REDIR_APPEND) {
            fd = open(redirection->target, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd == -1) {
                fprintf(stderr, "smash: cannot open %s: %s\n", redirection->target, strerror(errno));
                return 1;
            }

            if (dup2(fd, STDOUT_FILENO) == -1) {
                close(fd);
                return 1;
            }
        }

        if (fd != -1) {
            close(fd);
        }
    }

    return 0;
}

static int run_parent_builtin(SmashState *state, const SmashCommand *command) {
    int saved[3] = {-1, -1, -1};
    int status;

    saved[0] = dup(STDIN_FILENO);
    saved[1] = dup(STDOUT_FILENO);
    saved[2] = dup(STDERR_FILENO);

    if (saved[0] == -1 || saved[1] == -1 || saved[2] == -1) {
        fprintf(stderr, "smash: failed to save standard file descriptors\n");
        return 1;
    }

    if (apply_redirections(command) != 0) {
        dup2(saved[0], STDIN_FILENO);
        dup2(saved[1], STDOUT_FILENO);
        dup2(saved[2], STDERR_FILENO);
        close(saved[0]);
        close(saved[1]);
        close(saved[2]);
        return 1;
    }

    status = smash_run_builtin(state, command->argv, 0);

    dup2(saved[0], STDIN_FILENO);
    dup2(saved[1], STDOUT_FILENO);
    dup2(saved[2], STDERR_FILENO);
    close(saved[0]);
    close(saved[1]);
    close(saved[2]);
    return status;
}

static void run_child_command(
    SmashState *state,
    const SmashCommand *command,
    int input_fd,
    int output_fd
) {
    if (input_fd != -1 && dup2(input_fd, STDIN_FILENO) == -1) {
        _exit(1);
    }

    if (output_fd != -1 && dup2(output_fd, STDOUT_FILENO) == -1) {
        _exit(1);
    }

    if (apply_redirections(command) != 0) {
        _exit(1);
    }

    if (smash_is_builtin(command->argv[0])) {
        _exit(smash_run_builtin(state, command->argv, 1));
    }

    execvp(command->argv[0], command->argv);
    fprintf(stderr, "smash: command not found: %s\n", command->argv[0]);
    _exit(127);
}

int smash_execute_pipeline(SmashState *state, const SmashPipeline *pipeline) {
    pid_t *pids;
    size_t i;
    int previous_read = -1;
    int last_status = 0;

    if (!pipeline || pipeline->count == 0) {
        return 0;
    }

    if (pipeline->count == 1 && smash_is_builtin(pipeline->commands[0].argv[0])) {
        return run_parent_builtin(state, &pipeline->commands[0]);
    }

    pids = calloc(pipeline->count, sizeof(pid_t));
    if (!pids) {
        fprintf(stderr, "smash: allocation error\n");
        return 1;
    }

    smash_term_disable_raw(state);

    for (i = 0; i < pipeline->count; i++) {
        int pipe_fd[2] = {-1, -1};
        int child_input = previous_read;
        int child_output = -1;

        if (i + 1 < pipeline->count && pipe(pipe_fd) != 0) {
            fprintf(stderr, "smash: pipe failed: %s\n", strerror(errno));
            free(pids);
            smash_term_enable_raw(state);
            return 1;
        }

        if (i + 1 < pipeline->count) {
            child_output = pipe_fd[1];
        }

        pids[i] = fork();
        if (pids[i] == 0) {
            restore_default_signals();

            if (pipe_fd[0] != -1) close(pipe_fd[0]);
            if (previous_read != -1 && previous_read != child_input) close(previous_read);
            run_child_command(state, &pipeline->commands[i], child_input, child_output);
        }

        if (pids[i] < 0) {
            fprintf(stderr, "smash: fork failed: %s\n", strerror(errno));
            free(pids);
            smash_term_enable_raw(state);
            return 1;
        }

        if (child_input != -1) close(child_input);
        if (child_output != -1) close(child_output);
        previous_read = pipe_fd[0];
    }

    if (previous_read != -1) {
        close(previous_read);
    }

    for (i = 0; i < pipeline->count; i++) {
        int status = 0;

        if (waitpid(pids[i], &status, 0) == -1) {
            fprintf(stderr, "smash: waitpid failed: %s\n", strerror(errno));
            last_status = 1;
            continue;
        }

        if (WIFEXITED(status)) {
            last_status = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            last_status = 128 + WTERMSIG(status);
        }
    }

    free(pids);
    smash_term_enable_raw(state);
    return last_status;
}
