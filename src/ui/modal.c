#include "ui/modal.h"

#include "ui/theme.h"

#include <math.h>
#include <stddef.h>

typedef struct ModalLayout {
    Rectangle panel;
    Rectangle confirm;
    Rectangle cancel;
} ModalLayout;

static ModalLayout modal_layout(void)
{
    const float width = (float)GetScreenWidth();
    const float height = (float)GetScreenHeight();
    const float panel_width = fminf(440.0f, width - 64.0f);
    const float panel_height = 232.0f;
    const float x = (width - panel_width) * 0.5f;
    const float y = (height - panel_height) * 0.5f;
    const float gap = 12.0f;
    const float button_width = (panel_width - 48.0f - gap) * 0.5f;

    ModalLayout layout = {0};
    layout.panel = (Rectangle){x, y, panel_width, panel_height};
    layout.confirm = (Rectangle){
        .x = x + 24.0f,
        .y = y + panel_height - 24.0f - 46.0f,
        .width = button_width,
        .height = 46.0f
    };
    layout.cancel = (Rectangle){
        .x = layout.confirm.x + button_width + gap,
        .y = layout.confirm.y,
        .width = button_width,
        .height = 46.0f
    };
    return layout;
}

typedef struct ModalText {
    const char *title;
    const char *body;
    const char *confirm;
} ModalText;

static ModalText modal_texts(ModalPurpose purpose)
{
    switch (purpose) {
    case MODAL_PURPOSE_QUIT:
        return (ModalText){"Quit Reversi?",
                           "Your current game will close.", "Quit"};
    case MODAL_PURPOSE_RESTART:
        return (ModalText){"Restart this game?",
                           "The current board will be reset.", "Restart"};
    case MODAL_PURPOSE_RETURN_MENU:
        return (ModalText){"Return to main menu?",
                           "The current game will be lost.", "Leave"};
    default:
        return (ModalText){"", "", "Confirm"};
    }
}

void modal_init(Modal *modal)
{
    if (modal == NULL) {
        return;
    }

    ui_button_init(&modal->confirm, "Confirm", true);
    ui_button_init(&modal->cancel, "Cancel", false);
    modal->purpose = MODAL_PURPOSE_NONE;
    modal->appear = 0.0f;
}

bool modal_is_open(const Modal *modal)
{
    return modal != NULL && modal->purpose != MODAL_PURPOSE_NONE;
}

void modal_open(Modal *modal, ModalPurpose purpose)
{
    if (modal == NULL) {
        return;
    }

    const ModalText text = modal_texts(purpose);

    modal->purpose = purpose;
    modal->appear = 0.0f;
    ui_button_init(&modal->confirm, text.confirm, true);
    ui_button_init(&modal->cancel, "Cancel", false);

    const ModalLayout layout = modal_layout();
    ui_button_set_bounds(&modal->confirm, layout.confirm);
    ui_button_set_bounds(&modal->cancel, layout.cancel);
}

void modal_close(Modal *modal)
{
    if (modal != NULL) {
        modal->purpose = MODAL_PURPOSE_NONE;
        modal->appear = 0.0f;
    }
}

ModalAction modal_update(Modal *modal, const UiInput *input, float dt)
{
    if (!modal_is_open(modal) || input == NULL) {
        return MODAL_ACTION_NONE;
    }

    modal->appear = input->reduce_motion
        ? REVERSI_UI_DUR_MODAL : modal->appear + dt;
    const bool ready = modal->appear >= REVERSI_UI_DUR_MODAL;
    const ModalLayout layout = modal_layout();
    ui_button_set_bounds(&modal->confirm, layout.confirm);
    ui_button_set_bounds(&modal->cancel, layout.cancel);

    const bool confirm_clicked = ui_button_update(
        &modal->confirm, input, ready, dt);
    const bool cancel_clicked = ui_button_update(
        &modal->cancel, input, ready, dt);

    if (!ready) {
        return MODAL_ACTION_NONE;
    }

    if (confirm_clicked || input->enter_pressed) {
        return MODAL_ACTION_CONFIRM;
    }
    if (cancel_clicked || input->escape_pressed) {
        return MODAL_ACTION_CANCEL;
    }
    return MODAL_ACTION_NONE;
}

void modal_draw(const Modal *modal, const UiFonts *fonts)
{
    if (!modal_is_open(modal) || fonts == NULL) {
        return;
    }

    const float width = (float)GetScreenWidth();
    const float height = (float)GetScreenHeight();
    const float fade = ui_clamp01(modal->appear / REVERSI_UI_DUR_MODAL);

    DrawRectangle(0, 0, (int)width, (int)height,
                  ui_color_alpha(REVERSI_UI_COLOR_OVERLAY, 0.82f * fade));

    const ModalLayout layout = modal_layout();
    ui_draw_panel(layout.panel, REVERSI_UI_RADIUS_LG,
                  REVERSI_UI_COLOR_SURFACE, REVERSI_UI_COLOR_BORDER_STRONG);

    const ModalText text = modal_texts(modal->purpose);

    ui_draw_text_aligned(
        fonts, UI_TEXT_SEMIBOLD, text.title,
        (Rectangle){layout.panel.x + 24.0f, layout.panel.y + 28.0f,
                    layout.panel.width - 48.0f, 34.0f},
        REVERSI_UI_FONT_HEADING, REVERSI_UI_COLOR_TEXT, UI_ALIGN_LEFT);
    ui_draw_text_aligned(
        fonts, UI_TEXT_REGULAR, text.body,
        (Rectangle){layout.panel.x + 24.0f, layout.panel.y + 72.0f,
                    layout.panel.width - 48.0f, 24.0f},
        REVERSI_UI_FONT_SMALL, REVERSI_UI_COLOR_TEXT_MUTED, UI_ALIGN_LEFT);

    ui_button_draw(&modal->confirm, fonts, false);
    ui_button_draw(&modal->cancel, fonts, false);
}
