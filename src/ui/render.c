#include "ui/render.h"

#include <raylib.h>

void ui_draw_frame(void)
{
    const char *title = "Reversi";
    const int title_size = 48;
    const int screen_width = GetScreenWidth();
    const int screen_height = GetScreenHeight();
    const int title_width = MeasureText(title, title_size);

    const Color background = {20, 24, 32, 255};
    const Color title_color = {236, 240, 246, 255};

    BeginDrawing();
    ClearBackground(background);
    DrawText(title,
             (screen_width - title_width) / 2,
             (screen_height - title_size) / 2,
             title_size,
             title_color);
    EndDrawing();
}
