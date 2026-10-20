#ifndef REVERSI_UI_SAVES_VIEW_H
#define REVERSI_UI_SAVES_VIEW_H

#include "ui/draw.h"

#define REVERSI_SAVES_SLOT_COUNT 3
#define REVERSI_SAVES_ROW_COUNT (REVERSI_SAVES_SLOT_COUNT + 1)
#define REVERSI_SAVES_SUMMARY_CAP 80

typedef struct SaveSlotInfo {
    bool present;
    bool valid;
    char summary[REVERSI_SAVES_SUMMARY_CAP];
} SaveSlotInfo;

typedef enum SavesActionType {
    SAVES_ACTION_NONE = 0,
    SAVES_ACTION_BACK,
    SAVES_ACTION_LOAD,
    SAVES_ACTION_SAVE,
    SAVES_ACTION_DELETE
} SavesActionType;

typedef struct SavesAction {
    SavesActionType type;
    int row;
} SavesAction;

typedef struct SavesView {
    UiButton actions[REVERSI_SAVES_ROW_COUNT];
    UiButton deletes[REVERSI_SAVES_ROW_COUNT];
    UiButton back;
    int focus;
    bool focus_visible;
} SavesView;

void saves_view_init(SavesView *view);
SavesAction saves_view_update(
    SavesView *view, const UiInput *input, bool accept_input, float dt,
    bool saving, const SaveSlotInfo slots[REVERSI_SAVES_ROW_COUNT]);
void saves_view_draw(
    const SavesView *view, const UiFonts *fonts, bool saving,
    const SaveSlotInfo slots[REVERSI_SAVES_ROW_COUNT]);

#endif
