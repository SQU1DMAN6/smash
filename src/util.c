#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "smash/util.h"

void *smash_xmalloc(size_t size) {
    void *ptr = malloc(size);

    if (!ptr) {
        fprintf(stderr, "smash: allocation error\n");
        exit(EXIT_FAILURE);
    }

    return ptr;
}

void *smash_xrealloc(void *ptr, size_t size) {
    void *resized = realloc(ptr, size);

    if (!resized) {
        fprintf(stderr, "smash: allocation error\n");
        exit(EXIT_FAILURE);
    }

    return resized;
}

char *smash_strdup(const char *text) {
    size_t len = strlen(text) + 1;
    char *copy = smash_xmalloc(len);

    memcpy(copy, text, len);
    return copy;
}

char *smash_build_home_path(const char *filename) {
    const char *home = getenv("HOME");
    size_t len;
    char *path;

    if (!home || home[0] == '\0') {
        return NULL;
    }

    len = strlen(home) + strlen(filename) + 2;
    path = smash_xmalloc(len);
    snprintf(path, len, "%s/%s", home, filename);
    return path;
}

char *smash_trim_in_place(char *text) {
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

char *smash_expand_path(const char *path) {
    const char *home = getenv("HOME");
    size_t len;
    char *expanded;

    if (!path) {
        return smash_strdup("");
    }

    if (path[0] != '~' || (path[1] != '\0' && path[1] != '/')) {
        return smash_strdup(path);
    }

    if (!home || home[0] == '\0') {
        return smash_strdup(path);
    }

    len = strlen(home) + strlen(path);
    expanded = smash_xmalloc(len);
    snprintf(expanded, len, "%s%s", home, path + 1);
    return expanded;
}
