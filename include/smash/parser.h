#ifndef SMASH_PARSER_H
#define SMASH_PARSER_H

#include <stddef.h>

typedef enum {
    SMASH_REDIR_INPUT = 0,
    SMASH_REDIR_OUTPUT,
    SMASH_REDIR_APPEND,
    SMASH_REDIR_HEREDOC
} SmashRedirectionType;

typedef struct {
    SmashRedirectionType type;
    char *target;
    char *data;
} SmashRedirection;

typedef struct {
    char **argv;
    size_t argc;
    SmashRedirection *redirections;
    size_t redirection_count;
} SmashCommand;

typedef struct {
    SmashCommand *commands;
    size_t count;
} SmashPipeline;

typedef char *(*SmashHeredocReader)(void *context, const char *prompt, int save_history);

int smash_parse_line(
    const char *line,
    SmashHeredocReader reader,
    void *context,
    SmashPipeline *pipeline,
    char **error_message
);

void smash_destroy_pipeline(SmashPipeline *pipeline);

#endif
