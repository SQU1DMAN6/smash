#define _POSIX_C_SOURCE 200809L

#include <sys/ioctl.h>
#include <unistd.h>

#include "smash/term.h"

void smash_term_enable_raw(SmashState *state) {
    struct termios raw;

    if (!state->termios_initialized) {
        if (tcgetattr(STDIN_FILENO, &state->original_termios) == -1) {
            return;
        }

        state->termios_initialized = 1;
    }

    raw = state->original_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        return;
    }

    state->raw_mode_enabled = 1;
}

void smash_term_disable_raw(SmashState *state) {
    if (!state->termios_initialized || !state->raw_mode_enabled) {
        return;
    }

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &state->original_termios);
    state->raw_mode_enabled = 0;
}

int smash_term_columns(void) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        return ws.ws_col;
    }

    return 80;
}
