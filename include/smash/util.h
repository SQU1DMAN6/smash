#ifndef SMASH_UTIL_H
#define SMASH_UTIL_H

#include <stddef.h>

void *smash_xmalloc(size_t size);
void *smash_xrealloc(void *ptr, size_t size);
char *smash_strdup(const char *text);
char *smash_build_home_path(const char *filename);
char *smash_trim_in_place(char *text);
char *smash_expand_path(const char *path);

#endif
