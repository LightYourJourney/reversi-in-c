#ifndef REVERSI_STORAGE_TEXT_H
#define REVERSI_STORAGE_TEXT_H

#include <stdbool.h>

/* Splits the next LF-terminated line in place, stripping a trailing CR. */
bool storage_text_next_line(char **cursor, char **line);

/* Parses a complete base-10 integer within [min, max]. */
bool storage_text_parse_int(const char *text, int min, int max, int *out);

#endif
