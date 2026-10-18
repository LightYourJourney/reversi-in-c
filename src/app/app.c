#include "app/app.h"

#include "ui/render.h"

#include <raylib.h>

#include <stdio.h>

#define REVERSI_WINDOW_WIDTH 960
#define REVERSI_WINDOW_HEIGHT 720
#define REVERSI_TARGET_FPS 60

int app_run(int max_frames)
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(REVERSI_WINDOW_WIDTH, REVERSI_WINDOW_HEIGHT, "Reversi");

    if (!IsWindowReady()) {
        fprintf(stderr, "Reversi: failed to initialize the game window\n");
        return 1;
    }

    SetTargetFPS(REVERSI_TARGET_FPS);

    int rendered_frames = 0;
    while (!WindowShouldClose()) {
        ui_draw_frame();
        ++rendered_frames;

        if (max_frames > 0 && rendered_frames >= max_frames) {
            break;
        }
    }

    CloseWindow();
    return 0;
}
