#include "app/app.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *program)
{
    printf("Usage: %s [--frames N]\n", program);
    printf("  --frames N  render N frames and exit (smoke testing)\n");
    printf("  --help      show this help\n");
}

static bool parse_frames(const char *text, int *out_frames)
{
    char *end = NULL;
    const long value = strtol(text, &end, 10);

    if (end == text || *end != '\0' || value < 0 || value > 1000000) {
        return false;
    }

    *out_frames = (int)value;
    return true;
}

int main(int argc, char **argv)
{
    int max_frames = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--frames") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Reversi: --frames requires a value\n");
                return 2;
            }
            if (!parse_frames(argv[++i], &max_frames)) {
                fprintf(stderr, "Reversi: invalid frame count: %s\n", argv[i]);
                return 2;
            }
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Reversi: unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 2;
        }
    }

    return app_run(max_frames);
}
