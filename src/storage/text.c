#include "storage/text.h"

#include <stdlib.h>
#include <string.h>

bool storage_text_next_line(char **cursor, char **line)
{
    if (cursor == NULL || *cursor == NULL || **cursor == '\0'
        || line == NULL) {
        return false;
    }
    char *start = *cursor;
    char *newline = strchr(start, '\n');
    if (newline != NULL) {
        *newline = '\0';
        *cursor = newline + 1;
    } else {
        *cursor = start + strlen(start);
    }
    const size_t length = strlen(start);
    if (length > 0 && start[length - 1] == '\r') {
        start[length - 1] = '\0';
    }
    *line = start;
    return true;
}

bool storage_text_parse_int(const char *text, int min, int max, int *out)
{
    if (text == NULL || out == NULL) {
        return false;
    }
    char *end = NULL;
    const long value = strtol(text, &end, 10);
    if (end == text || *end != '\0' || value < (long)min || value > (long)max) {
        return false;
    }
    *out = (int)value;
    return true;
}
