#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "smash/history.h"
#include "smash/line_editor.h"
#include "smash/term.h"
#include "smash/util.h"
#include "smash/builtins.h"

#define SMASH_LINE_BUFFER 1024

static const char *ANSI_RESET = "\x1b[0m";
static const char *ANSI_GREEN = "\x1b[32m";
static const char *ANSI_BLUE = "\x1b[34m";
static const char *ANSI_RED = "\x1b[31m";
static const char *ANSI_GREY = "\x1b[90m";
static const char *ANSI_WHITE = "\x1b[37m";
static const char *ANSI_ORANGE = "\x1b[33m";

static int history_matches_prefix(const char *entry, const char *prefix) {
    if (!prefix) {
        return 1;
    }

    size_t prefix_len = strlen(prefix);
    return prefix_len == 0 || strncmp(entry, prefix, prefix_len) == 0;
}

static void flush_colored_segment(const char *buffer, int start, int end, const char *color) {
    if (end <= start) {
        return;
    }

    if (color == ANSI_WHITE) {
        write(STDOUT_FILENO, buffer + start, (size_t) (end - start));
        return;
    }

    write(STDOUT_FILENO, color, strlen(color));
    write(STDOUT_FILENO, buffer + start, (size_t) (end - start));
    write(STDOUT_FILENO, ANSI_RESET, strlen(ANSI_RESET));
}

static void highlight_line(const char *buffer) {
    int i = 0;
    int token_start = 0;
    int token_end = 0;
    int in_single = 0;
    int in_double = 0;
    int comment = 0;
    int in_token = 0;
    const char *color = ANSI_WHITE;
    const char *segment_color = ANSI_WHITE;
    int segment_start = 0;
    char *command_name = NULL;
    int command_valid = 0;

    while (buffer[token_end] && isspace((unsigned char)buffer[token_end])) {
        token_end++;
    }

    token_start = token_end;
    while (buffer[token_end] && !isspace((unsigned char)buffer[token_end])) {
        if (buffer[token_end] == '\'') {
            token_end++;
            while (buffer[token_end] && buffer[token_end] != '\'') {
                token_end++;
            }
        } else if (buffer[token_end] == '"') {
            token_end++;
            while (buffer[token_end] && buffer[token_end] != '"') {
                token_end++;
            }
        }
        if (buffer[token_end]) {
            token_end++;
        }
    }

    if (token_start < token_end && buffer[token_start] != '#') {
        int name_start = token_start;
        int name_end = token_end;
        if ((buffer[name_start] == '\'' || buffer[name_start] == '"') && name_end > name_start + 1) {
            name_start++;
            if (buffer[name_end - 1] == buffer[name_start - 1]) {
                name_end--;
            }
        }
        if (name_end > name_start) {
            command_name = smash_xmalloc((size_t)(name_end - name_start + 1));
            memcpy(command_name, buffer + name_start, (size_t)(name_end - name_start));
            command_name[name_end - name_start] = '\0';
            command_valid = smash_command_exists(command_name);
            free(command_name);
        }
    }

    while (buffer[i]) {
        const char *next_color = ANSI_WHITE;
        char ch = buffer[i];

        if (comment) {
            next_color = ANSI_GREY;
        } else if (in_single || in_double) {
            next_color = ANSI_ORANGE;
        } else if (ch == '#') {
            next_color = ANSI_GREY;
            comment = 1;
            in_token = 0;
        } else if (isspace((unsigned char)ch)) {
            next_color = ANSI_WHITE;
            in_token = 0;
        } else if (!in_token) {
            if (i == token_start) {
                next_color = command_valid ? ANSI_GREEN : ANSI_RED;
            } else if (ch == '-') {
                next_color = ANSI_BLUE;
            } else {
                next_color = ANSI_WHITE;
            }
            in_token = 1;
        } else {
            next_color = color;
        }

        if (!comment && ch == '\'' && !in_double) {
            in_single = !in_single;
            next_color = ANSI_ORANGE;
        } else if (!comment && ch == '"' && !in_single) {
            in_double = !in_double;
            next_color = ANSI_ORANGE;
        }

        if (next_color != segment_color) {
            flush_colored_segment(buffer, segment_start, i, segment_color);
            segment_start = i;
            segment_color = next_color;
        }

        color = next_color;
        i++;
    }

    flush_colored_segment(buffer, segment_start, i, segment_color);
}

enum {
    KEY_ARROW_UP = 1000,
    KEY_ARROW_DOWN,
    KEY_ARROW_RIGHT,
    KEY_ARROW_LEFT,
    KEY_CTRL_RIGHT,
    KEY_CTRL_LEFT,
    KEY_ALT_BACKSPACE,
    KEY_DELETE,
    KEY_HOME,
    KEY_END
};

static int read_key(void) {
    char c;

    if (read(STDIN_FILENO, &c, 1) != 1) {
        return -1;
    }

    if (c == '\r') {
        return '\n';
    }

    if (c == '\x1b') {
        char prefix;
        char seq[16];
        int len = 0;

        if (read(STDIN_FILENO, &prefix, 1) != 1) {
            return '\x1b';
        }

        if (prefix == 127 || prefix == 8) {
            return KEY_ALT_BACKSPACE;
        }

        if (prefix != '[' && prefix != 'O') {
            return 0;
        }

        while (len < (int) sizeof(seq) - 1) {
            if (read(STDIN_FILENO, &seq[len], 1) != 1) {
                return '\x1b';
            }

            if ((seq[len] >= '@' && seq[len] <= '~') || isalpha((unsigned char) seq[len])) {
                len++;
                break;
            }

            len++;
        }

        seq[len] = '\0';

        if (prefix == '[') {
            if (strcmp(seq, "A") == 0) return KEY_ARROW_UP;
            if (strcmp(seq, "B") == 0) return KEY_ARROW_DOWN;
            if (strcmp(seq, "C") == 0) return KEY_ARROW_RIGHT;
            if (strcmp(seq, "D") == 0) return KEY_ARROW_LEFT;
            if (strcmp(seq, "H") == 0 || strcmp(seq, "1~") == 0 || strcmp(seq, "7~") == 0) {
                return KEY_HOME;
            }
            if (strcmp(seq, "F") == 0 || strcmp(seq, "4~") == 0 || strcmp(seq, "8~") == 0) {
                return KEY_END;
            }
            if (strcmp(seq, "3~") == 0) return KEY_DELETE;
            if (strcmp(seq, "5C") == 0 || strcmp(seq, "1;5C") == 0) return KEY_CTRL_RIGHT;
            if (strcmp(seq, "5D") == 0 || strcmp(seq, "1;5D") == 0) return KEY_CTRL_LEFT;
        }

        if (prefix == 'O') {
            if (strcmp(seq, "A") == 0) return KEY_ARROW_UP;
            if (strcmp(seq, "B") == 0) return KEY_ARROW_DOWN;
            if (strcmp(seq, "C") == 0) return KEY_ARROW_RIGHT;
            if (strcmp(seq, "D") == 0) return KEY_ARROW_LEFT;
            if (strcmp(seq, "H") == 0) return KEY_HOME;
            if (strcmp(seq, "F") == 0) return KEY_END;
        }

        return 0;
    }

    return (unsigned char) c;
}

static void move_cursor_up(int rows) {
    char seq[32];

    if (rows <= 0) {
        return;
    }

    snprintf(seq, sizeof(seq), "\x1b[%dA", rows);
    write(STDOUT_FILENO, seq, strlen(seq));
}

static void move_cursor_right(int columns) {
    char seq[32];

    if (columns <= 0) {
        return;
    }

    snprintf(seq, sizeof(seq), "\x1b[%dC", columns);
    write(STDOUT_FILENO, seq, strlen(seq));
}

static void ensure_buffer_capacity(char **buffer, int *capacity, int needed) {
    while (needed >= *capacity) {
        *capacity += SMASH_LINE_BUFFER;
        *buffer = smash_xrealloc(*buffer, (size_t) *capacity);
    }
}

static int jump_word_left(const char *buffer, int cursor) {
    while (cursor > 0 && isspace((unsigned char) buffer[cursor - 1])) {
        cursor--;
    }

    while (cursor > 0 && !isspace((unsigned char) buffer[cursor - 1])) {
        cursor--;
    }

    return cursor;
}

static int jump_word_right(const char *buffer, int length, int cursor) {
    while (cursor < length && isspace((unsigned char) buffer[cursor])) {
        cursor++;
    }

    while (cursor < length && !isspace((unsigned char) buffer[cursor])) {
        cursor++;
    }

    return cursor;
}

static int find_path_component_start(const char *buffer, int cursor) {
    while (cursor > 0 && isspace((unsigned char) buffer[cursor - 1])) {
        cursor--;
    }

    while (cursor > 0 && buffer[cursor - 1] == '/') {
        cursor--;
    }

    while (cursor > 0 &&
           buffer[cursor - 1] != '/' &&
           !isspace((unsigned char) buffer[cursor - 1])) {
        cursor--;
    }

    return cursor;
}

static void delete_range(char *buffer, int *length, int *cursor, int start, int end) {
    if (start < 0 || end < start || end > *length) {
        return;
    }

    memmove(buffer + start, buffer + end, (size_t) (*length - end + 1));
    *length -= end - start;
    *cursor = start;
}

static void refresh_line(
    const char *prompt,
    const char *buffer,
    int length,
    int cursor,
    int previous_cursor
) {
    int columns = smash_term_columns();
    int prompt_width = (int) strlen(prompt);
    int previous_row = (prompt_width + previous_cursor) / columns;
    int end_row = (prompt_width + length) / columns;
    int cursor_row = (prompt_width + cursor) / columns;
    int cursor_col = (prompt_width + cursor) % columns;

    write(STDOUT_FILENO, "\r", 1);
    move_cursor_up(previous_row);
    write(STDOUT_FILENO, "\x1b[J", 3);
    write(STDOUT_FILENO, prompt, strlen(prompt));
    if (length > 0) {
        highlight_line(buffer);
    }

    write(STDOUT_FILENO, "\r", 1);
    move_cursor_up(end_row - cursor_row);
    move_cursor_right(cursor_col);
}

char *smash_read_line(SmashState *state, const char *prompt, int save_history) {
    int capacity = SMASH_LINE_BUFFER;
    int length = 0;
    int cursor = 0;
    size_t history_index = state->history_count;
    char *saved_buffer = NULL;
    char *history_prefix = NULL;
    int saved_length = 0;
    char *buffer = smash_xmalloc((size_t) capacity);

    buffer[0] = '\0';
    write(STDOUT_FILENO, prompt, strlen(prompt));

    while (1) {
        int key = read_key();
        int previous_cursor = cursor;

        if (key == -1) {
            free(buffer);
            free(saved_buffer);
            free(history_prefix);
            return NULL;
        }

        if (key == 4) {
            if (length == 0) {
                write(STDOUT_FILENO, "\n", 1);
                free(buffer);
                return NULL;
            }

            if (cursor < length) {
                memmove(buffer + cursor, buffer + cursor + 1, (size_t) (length - cursor));
                length--;
                buffer[length] = '\0';
                refresh_line(prompt, buffer, length, cursor, previous_cursor);
            }
            continue;
        }

        if (key == 3) {
            write(STDOUT_FILENO, "^C\n", 3);
            length = 0;
            cursor = 0;
            buffer[0] = '\0';
            history_index = state->history_count;
            free(saved_buffer);
            saved_buffer = NULL;
            free(history_prefix);
            history_prefix = NULL;
            refresh_line(prompt, buffer, length, cursor, previous_cursor);
            continue;
        }

        if (key == '\n') {
            buffer[length] = '\0';
            write(STDOUT_FILENO, "\n", 1);
            if (save_history && length > 0) {
                smash_history_add(state, buffer, 1);
            }
            free(saved_buffer);
            free(history_prefix);
            return buffer;
        }

        if (key == KEY_ARROW_UP) {
            if (!history_prefix) {
                history_prefix = smash_strdup(buffer);
                saved_buffer = smash_strdup(buffer);
                saved_length = length;
            }

            size_t search_index = history_index == state->history_count ? state->history_count : history_index;
            size_t found_index = state->history_count;
            for (size_t i = search_index; i-- > 0;) {
                if (history_matches_prefix(state->history[i], history_prefix)) {
                    found_index = i;
                    break;
                }
            }

            if (found_index < state->history_count) {
                history_index = found_index;
                length = (int) strlen(state->history[history_index]);
                ensure_buffer_capacity(&buffer, &capacity, length + 1);
                memcpy(buffer, state->history[history_index], (size_t) length + 1);
                cursor = length;
                refresh_line(prompt, buffer, length, cursor, previous_cursor);
            }
            continue;
        }

        if (key == KEY_ARROW_DOWN) {
            if (!history_prefix) {
                history_prefix = smash_strdup(buffer);
                saved_buffer = smash_strdup(buffer);
                saved_length = length;
            }

            size_t start_index = history_index == state->history_count ? 0 : history_index + 1;
            size_t found_index = state->history_count;
            for (size_t i = start_index; i < state->history_count; i++) {
                if (history_matches_prefix(state->history[i], history_prefix)) {
                    found_index = i;
                    break;
                }
            }

            if (found_index < state->history_count) {
                history_index = found_index;
                length = (int) strlen(state->history[history_index]);
                ensure_buffer_capacity(&buffer, &capacity, length + 1);
                memcpy(buffer, state->history[history_index], (size_t) length + 1);
                cursor = length;
            } else if (history_index != state->history_count) {
                history_index = state->history_count;
                if (saved_buffer) {
                    length = saved_length;
                    ensure_buffer_capacity(&buffer, &capacity, length + 1);
                    memcpy(buffer, saved_buffer, (size_t) length + 1);
                    cursor = length;
                } else {
                    length = 0;
                    cursor = 0;
                    buffer[0] = '\0';
                }
            }

            refresh_line(prompt, buffer, length, cursor, previous_cursor);
            continue;
        }

        if (key == KEY_ARROW_RIGHT && cursor < length) {
            cursor++;
            refresh_line(prompt, buffer, length, cursor, previous_cursor);
            continue;
        }

        if (key == KEY_ARROW_LEFT && cursor > 0) {
            cursor--;
            refresh_line(prompt, buffer, length, cursor, previous_cursor);
            continue;
        }

        if (key == KEY_CTRL_RIGHT) {
            cursor = jump_word_right(buffer, length, cursor);
            refresh_line(prompt, buffer, length, cursor, previous_cursor);
            continue;
        }

        if (key == KEY_CTRL_LEFT) {
            cursor = jump_word_left(buffer, cursor);
            refresh_line(prompt, buffer, length, cursor, previous_cursor);
            continue;
        }

        if (key == KEY_ALT_BACKSPACE && cursor > 0) {
            int start = jump_word_left(buffer, cursor);

            delete_range(buffer, &length, &cursor, start, cursor);
            refresh_line(prompt, buffer, length, cursor, previous_cursor);
            continue;
        }

        if (key == 23 && cursor > 0) {
            int start = find_path_component_start(buffer, cursor);

            delete_range(buffer, &length, &cursor, start, cursor);
            refresh_line(prompt, buffer, length, cursor, previous_cursor);
            continue;
        }

        if (key == KEY_HOME || key == 1) {
            cursor = 0;
            refresh_line(prompt, buffer, length, cursor, previous_cursor);
            continue;
        }

        if (key == KEY_END || key == 5) {
            cursor = length;
            refresh_line(prompt, buffer, length, cursor, previous_cursor);
            continue;
        }

        if (key == 12) {
            write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
            refresh_line(prompt, buffer, length, cursor, previous_cursor);
            continue;
        }

        if (key == KEY_DELETE && cursor < length) {
            memmove(buffer + cursor, buffer + cursor + 1, (size_t) (length - cursor));
            length--;
            buffer[length] = '\0';
            refresh_line(prompt, buffer, length, cursor, previous_cursor);
            continue;
        }

        if (key == 127 || key == 8) {
            if (cursor > 0) {
                memmove(buffer + cursor - 1, buffer + cursor, (size_t) (length - cursor + 1));
                length--;
                cursor--;
                refresh_line(prompt, buffer, length, cursor, previous_cursor);
            }
            continue;
        }

        if (key <= 0 || !isprint((unsigned char) key)) {
            continue;
        }

        ensure_buffer_capacity(&buffer, &capacity, length + 2);
        memmove(buffer + cursor + 1, buffer + cursor, (size_t) (length - cursor + 1));
        buffer[cursor] = (char) key;
        length++;
        cursor++;
        refresh_line(prompt, buffer, length, cursor, previous_cursor);
    }
}
