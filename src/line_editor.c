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

#define SMASH_LINE_BUFFER 1024

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
        write(STDOUT_FILENO, buffer, (size_t) length);
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
    char *buffer = smash_xmalloc((size_t) capacity);

    buffer[0] = '\0';
    write(STDOUT_FILENO, prompt, strlen(prompt));

    while (1) {
        int key = read_key();
        int previous_cursor = cursor;

        if (key == -1) {
            free(buffer);
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
            refresh_line(prompt, buffer, length, cursor, previous_cursor);
            continue;
        }

        if (key == '\n') {
            buffer[length] = '\0';
            write(STDOUT_FILENO, "\n", 1);
            if (save_history && length > 0) {
                smash_history_add(state, buffer, 1);
            }
            return buffer;
        }

        if (key == KEY_ARROW_UP && history_index > 0) {
            history_index--;
            length = (int) strlen(state->history[history_index]);
            ensure_buffer_capacity(&buffer, &capacity, length + 1);
            memcpy(buffer, state->history[history_index], (size_t) length + 1);
            cursor = length;
            refresh_line(prompt, buffer, length, cursor, previous_cursor);
            continue;
        }

        if (key == KEY_ARROW_DOWN && history_index < state->history_count - 1) {
            history_index++;
            length = (int) strlen(state->history[history_index]);
            ensure_buffer_capacity(&buffer, &capacity, length + 1);
            memcpy(buffer, state->history[history_index], (size_t) length + 1);
            cursor = length;
            refresh_line(prompt, buffer, length, cursor, previous_cursor);
            continue;
        }

        if (key == KEY_ARROW_DOWN && history_index == state->history_count - 1) {
            history_index = state->history_count;
            length = 0;
            cursor = 0;
            buffer[0] = '\0';
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
