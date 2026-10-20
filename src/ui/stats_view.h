#ifndef REVERSI_UI_STATS_VIEW_H
#define REVERSI_UI_STATS_VIEW_H

#include "storage/stats.h"
#include "ui/draw.h"

typedef enum StatsAction {
    STATS_ACTION_NONE = 0,
    STATS_ACTION_BACK
} StatsAction;

typedef struct StatsView {
    UiButton back;
    int focus;
    bool focus_visible;
} StatsView;

void stats_view_init(StatsView *view);
StatsAction stats_view_update(StatsView *view, const UiInput *input,
                              bool accept_input, float dt);
void stats_view_draw(const StatsView *view, const UiFonts *fonts,
                     const GameStats *stats);

#endif
