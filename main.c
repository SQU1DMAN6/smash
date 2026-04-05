#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define RED "\033[0;31m"
#define RESET "\033[0m"
#define PROMPT "=> "
#define HEREDOC_PROMPT " > "
#define TK_BUFF_SIZE 1024
#define HISTORY_SIZE 1024
#define HISTORY_FILE ".smash_history"
#define RC_FILE ".smashrc"
#define DEFAULT_SHELL "/bin/bash"
#define FALLBACK_SHELL "/bin/sh"

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

static struct termios orig_termios;
static int termios_initialized = 0;
static int raw_mode_enabled = 0;
static int smash_exit_code = 0;

static char *history[HISTORY_SIZE];
static int history_count = 0;
static char *history_path = NULL;
static char *smashrc_path = NULL;
static const char *command_shell = NULL;

static void disable_raw_mode(void);
static void enable_raw_mode(void);
static char *read_line_with_prompt(const char *prompt, int save_history);
static int read_key(void);
static int smash_execute(const char *line);

static char *duplicate_string(const char *source) {
    size_t len = strlen(source) + 1;
    char *copy = malloc(len);

    if (!copy) {
        fprintf(stderr, "%ssmash: allocation error%s\n", RED, RESET);
        exit(EXIT_FAILURE);
    }

    memcpy(copy, source, len);
    return copy;
}

static char *build_home_path(const char *filename) {
    const char *home = getenv("HOME");
    size_t len;
    char *path;

    if (!home || home[0] == '\0') {
        return NULL;
    }

    len = strlen(home) + strlen(filename) + 2;
    path = malloc(len);
    if (!path) {
        fprintf(stderr, "%ssmash: allocation error%s\n", RED, RESET);
        exit(EXIT_FAILURE);
    }

    snprintf(path, len, "%s/%s", home, filename);
    return path;
}

static char *trim_in_place(char *text) {
    char *end;

    while (*text && isspace((unsigned char) *text)) {
        text++;
    }

    if (*text == '\0') {
        return text;
    }

    end = text + strlen(text) - 1;
    while (end > text && isspace((unsigned char) *end)) {
        *end = '\0';
        end--;
    }

    return text;
}

static int starts_with_keyword(const char *line, const char *keyword) {
    size_t len = strlen(keyword);

    return strncmp(line, keyword, len) == 0 &&
           (line[len] == '\0' || isspace((unsigned char) line[len]));
}

static int has_unquoted_shell_metacharacters(const char *text) {
    int in_single = 0;
    int in_double = 0;
    int escaped = 0;

    while (*text) {
        char ch = *text++;

        if (escaped) {
            escaped = 0;
            continue;
        }

        if (ch == '\\' && !in_single) {
            escaped = 1;
            continue;
        }

        if (ch == '\'' && !in_double) {
            in_single = !in_single;
            continue;
        }

        if (ch == '"' && !in_single) {
            in_double = !in_double;
            continue;
        }

        if (!in_single && !in_double) {
            switch (ch) {
                case '|':
                case '&':
                case ';':
                case '<':
                case '>':
                case '(':
                case ')':
                case '$':
                case '`':
                    return 1;
            }
        }
    }

    return 0;
}

static char *expand_tilde_path(const char *path) {
    const char *home = getenv("HOME");
    size_t len;
    char *expanded;

    if (!path) {
        return duplicate_string("");
    }

    if (path[0] != '~' || (path[1] != '\0' && path[1] != '/')) {
        return duplicate_string(path);
    }

    if (!home || home[0] == '\0') {
        return duplicate_string(path);
    }

    len = strlen(home) + strlen(path);
    expanded = malloc(len);
    if (!expanded) {
        fprintf(stderr, "%ssmash: allocation error%s\n", RED, RESET);
        exit(EXIT_FAILURE);
    }

    snprintf(expanded, len, "%s%s", home, path + 1);
    return expanded;
}

static void ensure_buffer_capacity(char **buffer, int *buffsize, int needed) {
    char *resized;

    if (needed < *buffsize) {
        return;
    }

    while (needed >= *buffsize) {
        *buffsize += TK_BUFF_SIZE;
    }

    resized = realloc(*buffer, *buffsize);
    if (!resized) {
        free(*buffer);
        fprintf(stderr, "%ssmash: allocation error%s\n", RED, RESET);
        exit(EXIT_FAILURE);
    }

    *buffer = resized;
}

static int get_terminal_columns(void) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        return ws.ws_col;
    }

    return 80;
}

static void move_cursor_vertical(int rows) {
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

static void refresh_line(const char *prompt, const char *buffer, int pos, int cursor, int previous_cursor) {
    int columns = get_terminal_columns();
    int prompt_width = (int) strlen(prompt);
    int previous_row = (prompt_width + previous_cursor) / columns;
    int end_row = (prompt_width + pos) / columns;
    int cursor_row = (prompt_width + cursor) / columns;
    int cursor_col = (prompt_width + cursor) % columns;

    write(STDOUT_FILENO, "\r", 1);
    move_cursor_vertical(previous_row);
    write(STDOUT_FILENO, "\x1b[J", 3);
    write(STDOUT_FILENO, prompt, strlen(prompt));
    if (pos > 0) {
        write(STDOUT_FILENO, buffer, pos);
    }

    write(STDOUT_FILENO, "\r", 1);
    move_cursor_vertical(end_row - cursor_row);
    move_cursor_right(cursor_col);
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

static int jump_word_right(const char *buffer, int pos, int cursor) {
    while (cursor < pos && isspace((unsigned char) buffer[cursor])) {
        cursor++;
    }

    while (cursor < pos && !isspace((unsigned char) buffer[cursor])) {
        cursor++;
    }

    return cursor;
}

static int find_word_start(const char *buffer, int cursor) {
    while (cursor > 0 && isspace((unsigned char) buffer[cursor - 1])) {
        cursor--;
    }

    while (cursor > 0 && !isspace((unsigned char) buffer[cursor - 1])) {
        cursor--;
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

static void delete_range(char *buffer, int *pos, int *cursor, int start, int end) {
    if (start < 0 || end < start || end > *pos) {
        return;
    }

    memmove(buffer + start, buffer + end, *pos - end + 1);
    *pos -= end - start;
    *cursor = start;
}

static void add_history_entry(const char *line, int persist) {
    FILE *file;

    if (!line || line[0] == '\0') {
        return;
    }

    if (history_count == HISTORY_SIZE) {
        free(history[0]);
        memmove(history, history + 1, sizeof(history[0]) * (HISTORY_SIZE - 1));
        history_count--;
    }

    history[history_count++] = duplicate_string(line);

    if (!persist || !history_path) {
        return;
    }

    file = fopen(history_path, "a");
    if (!file) {
        return;
    }

    fprintf(file, "%s\n", line);
    fclose(file);
}

static void clear_history_entries(void) {
    int i;
    FILE *file;

    for (i = 0; i < history_count; i++) {
        free(history[i]);
        history[i] = NULL;
    }
    history_count = 0;

    if (!history_path) {
        return;
    }

    file = fopen(history_path, "w");
    if (file) {
        fclose(file);
    }
}

static void load_history(void) {
    FILE *file;
    char *line = NULL;
    size_t capacity = 0;
    ssize_t len;

    if (!history_path) {
        return;
    }

    file = fopen(history_path, "r");
    if (!file) {
        return;
    }

    while ((len = getline(&line, &capacity, file)) != -1) {
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }

        add_history_entry(line, 0);
    }

    free(line);
    fclose(file);
}

static void print_history(void) {
    int i;

    for (i = 0; i < history_count; i++) {
        printf("%4d  %s\n", i + 1, history[i]);
    }
}

static void append_text(char **buffer, size_t *length, size_t *capacity, const char *text) {
    size_t needed = *length + strlen(text) + 1;
    char *resized;

    if (needed > *capacity) {
        while (needed > *capacity) {
            *capacity += TK_BUFF_SIZE;
        }

        resized = realloc(*buffer, *capacity);
        if (!resized) {
            free(*buffer);
            fprintf(stderr, "%ssmash: allocation error%s\n", RED, RESET);
            exit(EXIT_FAILURE);
        }

        *buffer = resized;
    }

    memcpy(*buffer + *length, text, strlen(text) + 1);
    *length += strlen(text);
}

static int is_heredoc_operator(const char *text, int index) {
    return text[index] == '<' && text[index + 1] == '<' && text[index + 2] != '<';
}

static char *copy_heredoc_delimiter(const char *start, int *consumed) {
    int len = 0;

    if (*start == '\'' || *start == '"') {
        char quote = *start++;
        const char *content_start = start;

        while (start[len] && start[len] != quote) {
            len++;
        }

        if (start[len] == quote) {
            *consumed = len + 2;
            return strndup(content_start, len);
        }

        *consumed = len + 1;
        return strndup(content_start, len);
    }

    while (start[len] &&
           !isspace((unsigned char) start[len]) &&
           start[len] != '<' &&
           start[len] != '>' &&
           start[len] != '|' &&
           start[len] != '&' &&
           start[len] != ';' &&
           start[len] != '(' &&
           start[len] != ')') {
        len++;
    }

    *consumed = len;
    return strndup(start, len);
}

static char **parse_heredoc_delimiters(const char *line, int *count) {
    char **delimiters = NULL;
    int capacity = 0;
    int in_single = 0;
    int in_double = 0;
    int escaped = 0;
    int i = 0;

    *count = 0;

    while (line[i]) {
        char ch = line[i];

        if (escaped) {
            escaped = 0;
            i++;
            continue;
        }

        if (ch == '\\' && !in_single) {
            escaped = 1;
            i++;
            continue;
        }

        if (ch == '\'' && !in_double) {
            in_single = !in_single;
            i++;
            continue;
        }

        if (ch == '"' && !in_single) {
            in_double = !in_double;
            i++;
            continue;
        }

        if (!in_single && !in_double && is_heredoc_operator(line, i)) {
            int consumed = 0;
            char *delimiter;

            i += 2;
            if (line[i] == '-') {
                i++;
            }

            while (line[i] && isspace((unsigned char) line[i])) {
                i++;
            }

            if (!line[i]) {
                break;
            }

            delimiter = copy_heredoc_delimiter(line + i, &consumed);
            if (!delimiter || delimiter[0] == '\0') {
                free(delimiter);
                break;
            }

            if (*count == capacity) {
                char **resized;

                capacity = capacity == 0 ? 4 : capacity * 2;
                resized = realloc(delimiters, sizeof(char *) * capacity);
                if (!resized) {
                    int j;

                    for (j = 0; j < *count; j++) {
                        free(delimiters[j]);
                    }
                    free(delimiters);
                    fprintf(stderr, "%ssmash: allocation error%s\n", RED, RESET);
                    exit(EXIT_FAILURE);
                }

                delimiters = resized;
            }

            delimiters[*count] = delimiter;
            (*count)++;
            i += consumed;
            continue;
        }

        i++;
    }

    return delimiters;
}

static char *collect_heredoc_script(const char *line) {
    char **delimiters;
    int delimiter_count = 0;
    int i;
    char *script;
    size_t length;
    size_t capacity;

    delimiters = parse_heredoc_delimiters(line, &delimiter_count);
    if (delimiter_count == 0) {
        return duplicate_string(line);
    }

    capacity = strlen(line) + TK_BUFF_SIZE;
    script = malloc(capacity);
    if (!script) {
        fprintf(stderr, "%ssmash: allocation error%s\n", RED, RESET);
        exit(EXIT_FAILURE);
    }

    script[0] = '\0';
    length = 0;
    append_text(&script, &length, &capacity, line);
    append_text(&script, &length, &capacity, "\n");

    for (i = 0; i < delimiter_count; i++) {
        while (1) {
            char *heredoc_line = read_line_with_prompt(HEREDOC_PROMPT, 0);

            if (!heredoc_line) {
                fprintf(stderr,
                        "%ssmash: warning: here-document delimited by end-of-file (wanted `%s`)%s\n",
                        RED, delimiters[i], RESET);
                break;
            }

            append_text(&script, &length, &capacity, heredoc_line);
            append_text(&script, &length, &capacity, "\n");

            if (strcmp(heredoc_line, delimiters[i]) == 0) {
                free(heredoc_line);
                break;
            }

            free(heredoc_line);
        }
    }

    for (i = 0; i < delimiter_count; i++) {
        free(delimiters[i]);
    }
    free(delimiters);

    return script;
}

static void free_shell_state(void) {
    int i;

    free(history_path);
    free(smashrc_path);

    for (i = 0; i < history_count; i++) {
        free(history[i]);
    }
}

static const char *path_basename(const char *path) {
    const char *slash = strrchr(path, '/');

    return slash ? slash + 1 : path;
}

static void restore_default_signal_handlers(void) {
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
}

static void ignore_shell_signals(void) {
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
}

static const char *choose_command_shell(void) {
    const char *env_shell = getenv("SHELL");

    if (access(DEFAULT_SHELL, X_OK) == 0) {
        return DEFAULT_SHELL;
    }

    if (env_shell && access(env_shell, X_OK) == 0) {
        return env_shell;
    }

    return FALLBACK_SHELL;
}

static void configure_shell_environment(void) {
    setenv("SHELL", command_shell, 1);
    setenv("SMASH_BACKEND_SHELL", command_shell, 1);

    if (strcmp(path_basename(command_shell), "bash") != 0) {
        return;
    }

    if (smashrc_path && access(smashrc_path, F_OK) == 0) {
        setenv("BASH_ENV", smashrc_path, 1);
    } else {
        unsetenv("BASH_ENV");
    }
}

static void update_pwd_environment(void) {
    char cwd[PATH_MAX];

    if (getcwd(cwd, sizeof(cwd))) {
        setenv("PWD", cwd, 1);
    }
}

static void disable_raw_mode(void) {
    if (!termios_initialized || !raw_mode_enabled) {
        return;
    }

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    raw_mode_enabled = 0;
}

static void enable_raw_mode(void) {
    struct termios raw;

    if (!termios_initialized) {
        if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
            return;
        }
        termios_initialized = 1;
        atexit(disable_raw_mode);
    }

    raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        return;
    }

    raw_mode_enabled = 1;
}

static int run_command_with_shell(const char *line) {
    pid_t child_pid;
    int status = 0;

    disable_raw_mode();
    child_pid = fork();

    if (child_pid == 0) {
        restore_default_signal_handlers();
        execl(command_shell, path_basename(command_shell), "-c", line, (char *) NULL);
        fprintf(stderr, "%ssmash: failed to launch %s: %s%s\n",
                RED, command_shell, strerror(errno), RESET);
        _exit(EXIT_FAILURE);
    }

    if (child_pid < 0) {
        enable_raw_mode();
        fprintf(stderr, "%ssmash: fork failed: %s%s\n", RED, strerror(errno), RESET);
        return 1;
    }

    while (waitpid(child_pid, &status, 0) == -1) {
        if (errno != EINTR) {
            fprintf(stderr, "%ssmash: waitpid failed: %s%s\n", RED, strerror(errno), RESET);
            break;
        }
    }

    enable_raw_mode();
    return 1;
}

static int smash_exit(int exit_code) {
    smash_exit_code = exit_code;
    return 0;
}

static int smash_cd(const char *line) {
    char previous_dir[PATH_MAX];
    char *working = duplicate_string(line + 2);
    char *target_text = trim_in_place(working);
    char *expanded = NULL;
    const char *target = NULL;
    int print_new_dir = 0;

    if (!getcwd(previous_dir, sizeof(previous_dir))) {
        previous_dir[0] = '\0';
    }

    if (*target_text == '\0') {
        target = getenv("HOME");
    } else if (strcmp(target_text, "-") == 0) {
        target = getenv("OLDPWD");
        print_new_dir = 1;
    } else {
        size_t len = strlen(target_text);

        if (len >= 2 &&
            ((target_text[0] == '"' && target_text[len - 1] == '"') ||
             (target_text[0] == '\'' && target_text[len - 1] == '\''))) {
            target_text[len - 1] = '\0';
            target_text++;
        }
        target = target_text;
    }

    if (!target || target[0] == '\0') {
        fprintf(stderr, "%ssmash: cd: target directory not set%s\n", RED, RESET);
        free(working);
        return 1;
    }

    expanded = expand_tilde_path(target);
    if (chdir(expanded) != 0) {
        fprintf(stderr, "%ssmash: cd: %s: %s%s\n", RED, expanded, strerror(errno), RESET);
        free(expanded);
        free(working);
        return 1;
    }

    if (previous_dir[0] != '\0') {
        setenv("OLDPWD", previous_dir, 1);
    }
    update_pwd_environment();

    if (print_new_dir) {
        const char *cwd = getenv("PWD");
        if (cwd) {
            printf("%s\n", cwd);
        }
    }

    free(expanded);
    free(working);
    return 1;
}

static int smash_history_command(const char *line) {
    char *working = duplicate_string(line + strlen("history"));
    char *arg_text = trim_in_place(working);

    if (*arg_text == '\0') {
        print_history();
    } else if (strcmp(arg_text, "-c") == 0) {
        clear_history_entries();
    } else {
        fprintf(stderr, "%ssmash: history: unsupported option: %s%s\n",
                RED, arg_text, RESET);
    }

    free(working);
    return 1;
}

static int smash_pwd_command(void) {
    char cwd[PATH_MAX];

    if (!getcwd(cwd, sizeof(cwd))) {
        fprintf(stderr, "%ssmash: pwd: %s%s\n", RED, strerror(errno), RESET);
        return 1;
    }

    printf("%s\n", cwd);
    return 1;
}

static int smash_clear_command(void) {
    write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
    return 1;
}

static int smash_help_command(void) {
    static const char *message =
        "Builtins: cd, clear, exit, help, history, pwd\n"
        "Editing: Ctrl+Left/Right word jump, Alt+Backspace word delete,\n"
        "Ctrl+W path-component delete, Ctrl+A/E home/end, Ctrl+L clear screen.\n";

    write(STDOUT_FILENO, message, strlen(message));
    return 1;
}

static int smash_exit_command(const char *line) {
    char *working = duplicate_string(line + strlen("exit"));
    char *arg_text = trim_in_place(working);
    char *end = NULL;
    long code = 0;

    if (*arg_text == '\0') {
        free(working);
        return smash_exit(0);
    }

    errno = 0;
    code = strtol(arg_text, &end, 10);
    while (end && *end && isspace((unsigned char) *end)) {
        end++;
    }

    if (errno != 0 || !end || *end != '\0') {
        fprintf(stderr, "%ssmash: exit: numeric argument required%s\n", RED, RESET);
        free(working);
        return smash_exit(2);
    }

    free(working);
    return smash_exit((int) (code & 0xff));
}

static int smash_execute(const char *line) {
    char *working = duplicate_string(line);
    char *trimmed = trim_in_place(working);
    int use_builtin = !has_unquoted_shell_metacharacters(trimmed);
    int status = 1;

    if (*trimmed == '\0') {
        free(working);
        return 1;
    }

    if (use_builtin && starts_with_keyword(trimmed, "exit")) {
        status = smash_exit_command(trimmed);
    } else if (use_builtin && starts_with_keyword(trimmed, "cd")) {
        status = smash_cd(trimmed);
    } else if (use_builtin && starts_with_keyword(trimmed, "history")) {
        status = smash_history_command(trimmed);
    } else if (use_builtin && strcmp(trimmed, "pwd") == 0) {
        status = smash_pwd_command();
    } else if (use_builtin && strcmp(trimmed, "clear") == 0) {
        status = smash_clear_command();
    } else if (use_builtin && strcmp(trimmed, "help") == 0) {
        status = smash_help_command();
    } else {
        status = run_command_with_shell(line);
    }

    free(working);
    return status;
}

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

static char *read_line_with_prompt(const char *prompt, int save_history) {
    int buffsize = TK_BUFF_SIZE;
    int pos = 0;
    int cursor = 0;
    int history_index = history_count;
    char *buffer = malloc(buffsize);

    if (!buffer) {
        fprintf(stderr, "%ssmash: allocation error%s\n", RED, RESET);
        exit(EXIT_FAILURE);
    }

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
            if (pos == 0) {
                write(STDOUT_FILENO, "\n", 1);
                free(buffer);
                return NULL;
            }

            if (cursor < pos) {
                memmove(buffer + cursor, buffer + cursor + 1, pos - cursor - 1);
                pos--;
                buffer[pos] = '\0';
                refresh_line(prompt, buffer, pos, cursor, previous_cursor);
            }
            continue;
        }

        if (key == 3) {
            write(STDOUT_FILENO, "^C\n", 3);
            pos = 0;
            cursor = 0;
            buffer[0] = '\0';
            history_index = history_count;
            refresh_line(prompt, buffer, pos, cursor, previous_cursor);
            continue;
        }

        if (key == '\n') {
            buffer[pos] = '\0';
            write(STDOUT_FILENO, "\n", 1);
            if (save_history && pos > 0) {
                add_history_entry(buffer, 1);
            }
            return buffer;
        }

        if (key == KEY_ARROW_UP && history_index > 0) {
            history_index--;
            pos = (int) strlen(history[history_index]);
            ensure_buffer_capacity(&buffer, &buffsize, pos + 1);
            memcpy(buffer, history[history_index], pos + 1);
            cursor = pos;
            refresh_line(prompt, buffer, pos, cursor, previous_cursor);
            continue;
        }

        if (key == KEY_ARROW_DOWN && history_index < history_count - 1) {
            history_index++;
            pos = (int) strlen(history[history_index]);
            ensure_buffer_capacity(&buffer, &buffsize, pos + 1);
            memcpy(buffer, history[history_index], pos + 1);
            cursor = pos;
            refresh_line(prompt, buffer, pos, cursor, previous_cursor);
            continue;
        }

        if (key == KEY_ARROW_DOWN && history_index == history_count - 1) {
            history_index = history_count;
            pos = 0;
            cursor = 0;
            buffer[0] = '\0';
            refresh_line(prompt, buffer, pos, cursor, previous_cursor);
            continue;
        }

        if (key == KEY_ARROW_RIGHT && cursor < pos) {
            cursor++;
            refresh_line(prompt, buffer, pos, cursor, previous_cursor);
            continue;
        }

        if (key == KEY_ARROW_LEFT && cursor > 0) {
            cursor--;
            refresh_line(prompt, buffer, pos, cursor, previous_cursor);
            continue;
        }

        if (key == KEY_CTRL_RIGHT) {
            cursor = jump_word_right(buffer, pos, cursor);
            refresh_line(prompt, buffer, pos, cursor, previous_cursor);
            continue;
        }

        if (key == KEY_CTRL_LEFT) {
            cursor = jump_word_left(buffer, cursor);
            refresh_line(prompt, buffer, pos, cursor, previous_cursor);
            continue;
        }

        if (key == KEY_ALT_BACKSPACE && cursor > 0) {
            int start = find_word_start(buffer, cursor);

            delete_range(buffer, &pos, &cursor, start, cursor);
            refresh_line(prompt, buffer, pos, cursor, previous_cursor);
            continue;
        }

        if (key == 23 && cursor > 0) {
            int start = find_path_component_start(buffer, cursor);

            delete_range(buffer, &pos, &cursor, start, cursor);
            refresh_line(prompt, buffer, pos, cursor, previous_cursor);
            continue;
        }

        if (key == KEY_HOME || key == 1) {
            cursor = 0;
            refresh_line(prompt, buffer, pos, cursor, previous_cursor);
            continue;
        }

        if (key == KEY_END || key == 5) {
            cursor = pos;
            refresh_line(prompt, buffer, pos, cursor, previous_cursor);
            continue;
        }

        if (key == 12) {
            write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
            refresh_line(prompt, buffer, pos, cursor, previous_cursor);
            continue;
        }

        if (key == KEY_DELETE && cursor < pos) {
            memmove(buffer + cursor, buffer + cursor + 1, pos - cursor - 1);
            pos--;
            buffer[pos] = '\0';
            refresh_line(prompt, buffer, pos, cursor, previous_cursor);
            continue;
        }

        if (key == 127 || key == 8) {
            if (cursor > 0) {
                memmove(buffer + cursor - 1, buffer + cursor, pos - cursor);
                pos--;
                cursor--;
                buffer[pos] = '\0';
                refresh_line(prompt, buffer, pos, cursor, previous_cursor);
            }
            continue;
        }

        if (key <= 0 || !isprint((unsigned char) key)) {
            continue;
        }

        ensure_buffer_capacity(&buffer, &buffsize, pos + 2);
        memmove(buffer + cursor + 1, buffer + cursor, pos - cursor);
        buffer[cursor] = (char) key;
        pos++;
        cursor++;
        buffer[pos] = '\0';
        refresh_line(prompt, buffer, pos, cursor, previous_cursor);
    }
}

static void loop(void) {
    int status = 1;

    while (status) {
        char *line;
        char *script;

        line = read_line_with_prompt(PROMPT, 1);
        if (!line) {
            smash_exit_code = 0;
            break;
        }

        script = collect_heredoc_script(line);
        status = smash_execute(script);
        free(script);
        free(line);
    }
}

int main(void) {
    command_shell = choose_command_shell();
    history_path = build_home_path(HISTORY_FILE);
    smashrc_path = build_home_path(RC_FILE);

    atexit(free_shell_state);
    ignore_shell_signals();
    configure_shell_environment();
    update_pwd_environment();
    load_history();

    printf("SMASH 0.1.0, written by Quan Thai\n");
    enable_raw_mode();
    loop();
    return smash_exit_code;
}
