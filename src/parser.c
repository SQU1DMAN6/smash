#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "smash/parser.h"
#include "smash/util.h"

#define HEREDOC_PROMPT " > "

typedef enum {
    TOKEN_WORD = 0,
    TOKEN_PIPE,
    TOKEN_REDIR_INPUT,
    TOKEN_REDIR_OUTPUT,
    TOKEN_REDIR_APPEND,
    TOKEN_HEREDOC
} TokenType;

typedef struct {
    TokenType type;
    char *text;
} Token;

typedef struct {
    Token *items;
    size_t count;
    size_t capacity;
} TokenList;

static void append_char(char **buffer, size_t *length, size_t *capacity, char ch) {
    if (*length + 2 > *capacity) {
        *capacity = *capacity == 0 ? 32 : *capacity * 2;
        *buffer = smash_xrealloc(*buffer, *capacity);
    }

    (*buffer)[(*length)++] = ch;
    (*buffer)[*length] = '\0';
}

static void append_text(char **buffer, size_t *length, size_t *capacity, const char *text) {
    size_t i;

    for (i = 0; text[i] != '\0'; i++) {
        append_char(buffer, length, capacity, text[i]);
    }
}

static int push_token(TokenList *tokens, TokenType type, char *text) {
    if (tokens->count == tokens->capacity) {
        tokens->capacity = tokens->capacity == 0 ? 8 : tokens->capacity * 2;
        tokens->items = smash_xrealloc(tokens->items, tokens->capacity * sizeof(Token));
    }

    tokens->items[tokens->count].type = type;
    tokens->items[tokens->count].text = text;
    tokens->count++;
    return 1;
}

static void destroy_tokens(TokenList *tokens) {
    size_t i;

    for (i = 0; i < tokens->count; i++) {
        free(tokens->items[i].text);
    }

    free(tokens->items);
}

static int is_operator_character(char ch) {
    return ch == '|' || ch == '<' || ch == '>';
}

static int append_environment_value(
    const char *line,
    size_t *index,
    char **buffer,
    size_t *length,
    size_t *capacity
) {
    char name[256];
    size_t name_length = 0;
    const char *value;

    if (line[*index + 1] == '{') {
        *index += 2;
        while (line[*index] && line[*index] != '}' && name_length + 1 < sizeof(name)) {
            name[name_length++] = line[*index];
            (*index)++;
        }

        if (line[*index] == '}') {
            (*index)++;
        }
    } else {
        (*index)++;
        while ((isalnum((unsigned char) line[*index]) || line[*index] == '_') &&
               name_length + 1 < sizeof(name)) {
            name[name_length++] = line[*index];
            (*index)++;
        }
    }

    if (name_length == 0) {
        append_char(buffer, length, capacity, '$');
        return 1;
    }

    name[name_length] = '\0';
    value = getenv(name);
    if (value) {
        append_text(buffer, length, capacity, value);
    }

    return 1;
}

static int read_word_token(const char *line, size_t *index, TokenList *tokens, char **error_message) {
    char *buffer = NULL;
    size_t length = 0;
    size_t capacity = 0;

    while (line[*index] &&
           !isspace((unsigned char) line[*index]) &&
           !is_operator_character(line[*index])) {
        char ch = line[*index];

        if (ch == '\'') {
            (*index)++;
            while (line[*index] && line[*index] != '\'') {
                append_char(&buffer, &length, &capacity, line[*index]);
                (*index)++;
            }

            if (line[*index] != '\'') {
                free(buffer);
                *error_message = smash_strdup("unterminated single quote");
                return 0;
            }

            (*index)++;
            continue;
        }

        if (ch == '"') {
            (*index)++;
            while (line[*index] && line[*index] != '"') {
                if (line[*index] == '\\' && line[*index + 1] != '\0') {
                    append_char(&buffer, &length, &capacity, line[*index + 1]);
                    *index += 2;
                    continue;
                }

                if (line[*index] == '$') {
                    append_environment_value(line, index, &buffer, &length, &capacity);
                    continue;
                }

                append_char(&buffer, &length, &capacity, line[*index]);
                (*index)++;
            }

            if (line[*index] != '"') {
                free(buffer);
                *error_message = smash_strdup("unterminated double quote");
                return 0;
            }

            (*index)++;
            continue;
        }

        if (ch == '\\' && line[*index + 1] != '\0') {
            append_char(&buffer, &length, &capacity, line[*index + 1]);
            *index += 2;
            continue;
        }

        if (ch == '$') {
            append_environment_value(line, index, &buffer, &length, &capacity);
            continue;
        }

        append_char(&buffer, &length, &capacity, ch);
        (*index)++;
    }

    if (!buffer) {
        buffer = smash_strdup("");
    }

    return push_token(tokens, TOKEN_WORD, buffer);
}

static int tokenize_line(const char *line, TokenList *tokens, char **error_message) {
    size_t i = 0;

    while (line[i]) {
        if (isspace((unsigned char) line[i])) {
            i++;
            continue;
        }

        if (line[i] == '|') {
            push_token(tokens, TOKEN_PIPE, NULL);
            i++;
            continue;
        }

        if (line[i] == '<' && line[i + 1] == '<') {
            push_token(tokens, TOKEN_HEREDOC, NULL);
            i += 2;
            continue;
        }

        if (line[i] == '>' && line[i + 1] == '>') {
            push_token(tokens, TOKEN_REDIR_APPEND, NULL);
            i += 2;
            continue;
        }

        if (line[i] == '<') {
            push_token(tokens, TOKEN_REDIR_INPUT, NULL);
            i++;
            continue;
        }

        if (line[i] == '>') {
            push_token(tokens, TOKEN_REDIR_OUTPUT, NULL);
            i++;
            continue;
        }

        if (line[i] == ';' || line[i] == '&') {
            *error_message = smash_strdup("unsupported control operator");
            return 0;
        }

        if (!read_word_token(line, &i, tokens, error_message)) {
            return 0;
        }
    }

    return 1;
}

static void add_argument(SmashCommand *command, char *text) {
    command->argv = smash_xrealloc(command->argv, sizeof(char *) * (command->argc + 2));
    command->argv[command->argc] = text;
    command->argc++;
    command->argv[command->argc] = NULL;
}

static void add_redirection(
    SmashCommand *command,
    SmashRedirectionType type,
    char *target,
    char *data
) {
    command->redirections = smash_xrealloc(
        command->redirections,
        sizeof(SmashRedirection) * (command->redirection_count + 1)
    );

    command->redirections[command->redirection_count].type = type;
    command->redirections[command->redirection_count].target = target;
    command->redirections[command->redirection_count].data = data;
    command->redirection_count++;
}

static void add_command(SmashPipeline *pipeline, SmashCommand *command) {
    pipeline->commands = smash_xrealloc(
        pipeline->commands,
        sizeof(SmashCommand) * (pipeline->count + 1)
    );

    pipeline->commands[pipeline->count] = *command;
    pipeline->count++;
    memset(command, 0, sizeof(*command));
}

static char *read_heredoc_data(
    const char *delimiter,
    SmashHeredocReader reader,
    void *context,
    char **error_message
) {
    char *buffer = smash_strdup("");
    size_t length = 0;
    size_t capacity = 1;

    while (1) {
        char *line;

        if (!reader) {
            free(buffer);
            *error_message = smash_strdup("heredoc requires an interactive reader");
            return NULL;
        }

        line = reader(context, HEREDOC_PROMPT, 0);
        if (!line) {
            free(buffer);
            *error_message = smash_strdup("unexpected end of input while reading heredoc");
            return NULL;
        }

        if (strcmp(line, delimiter) == 0) {
            free(line);
            break;
        }

        append_text(&buffer, &length, &capacity, line);
        append_char(&buffer, &length, &capacity, '\n');
        free(line);
    }

    return buffer;
}

int smash_parse_line(
    const char *line,
    SmashHeredocReader reader,
    void *context,
    SmashPipeline *pipeline,
    char **error_message
) {
    TokenList tokens = {0};
    SmashCommand current = {0};
    size_t i;

    memset(pipeline, 0, sizeof(*pipeline));
    *error_message = NULL;

    if (!tokenize_line(line, &tokens, error_message)) {
        destroy_tokens(&tokens);
        return 0;
    }

    if (tokens.count == 0) {
        destroy_tokens(&tokens);
        return 1;
    }

    for (i = 0; i < tokens.count; i++) {
        Token *token = &tokens.items[i];

        if (token->type == TOKEN_WORD) {
            add_argument(&current, smash_strdup(token->text));
            continue;
        }

        if (token->type == TOKEN_PIPE) {
            if (current.argc == 0) {
                *error_message = smash_strdup("pipe without a command");
                smash_destroy_pipeline(pipeline);
                destroy_tokens(&tokens);
                return 0;
            }

            add_command(pipeline, &current);
            continue;
        }

        if (token->type == TOKEN_REDIR_INPUT ||
            token->type == TOKEN_REDIR_OUTPUT ||
            token->type == TOKEN_REDIR_APPEND ||
            token->type == TOKEN_HEREDOC) {
            char *target;
            char *data = NULL;
            SmashRedirectionType type;

            if (i + 1 >= tokens.count || tokens.items[i + 1].type != TOKEN_WORD) {
                *error_message = smash_strdup("redirection missing a target");
                smash_destroy_pipeline(pipeline);
                destroy_tokens(&tokens);
                return 0;
            }

            target = smash_strdup(tokens.items[++i].text);

            switch (token->type) {
                case TOKEN_REDIR_INPUT:
                    type = SMASH_REDIR_INPUT;
                    break;
                case TOKEN_REDIR_OUTPUT:
                    type = SMASH_REDIR_OUTPUT;
                    break;
                case TOKEN_REDIR_APPEND:
                    type = SMASH_REDIR_APPEND;
                    break;
                case TOKEN_HEREDOC:
                    type = SMASH_REDIR_HEREDOC;
                    data = read_heredoc_data(target, reader, context, error_message);
                    if (!data) {
                        free(target);
                        smash_destroy_pipeline(pipeline);
                        destroy_tokens(&tokens);
                        return 0;
                    }
                    break;
                default:
                    type = SMASH_REDIR_INPUT;
                    break;
            }

            add_redirection(&current, type, target, data);
        }
    }

    if (current.argc == 0) {
        *error_message = smash_strdup("missing command");
        smash_destroy_pipeline(pipeline);
        destroy_tokens(&tokens);
        return 0;
    }

    add_command(pipeline, &current);
    destroy_tokens(&tokens);
    return 1;
}

void smash_destroy_pipeline(SmashPipeline *pipeline) {
    size_t i;
    size_t j;

    for (i = 0; i < pipeline->count; i++) {
        SmashCommand *command = &pipeline->commands[i];

        for (j = 0; j < command->argc; j++) {
            free(command->argv[j]);
        }
        free(command->argv);

        for (j = 0; j < command->redirection_count; j++) {
            free(command->redirections[j].target);
            free(command->redirections[j].data);
        }
        free(command->redirections);
    }

    free(pipeline->commands);
    pipeline->commands = NULL;
    pipeline->count = 0;
}
