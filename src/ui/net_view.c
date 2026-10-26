#include "ui/net_view.h"

#include "ui/theme.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct NetField {
    Rectangle bounds;
    const char *label;
    char *value;
    size_t capacity;
    bool numeric;
} NetField;

static void set_button(NetView *view, int index, const char *label,
                       bool primary, Rectangle bounds, bool enabled)
{
    UiButton *button = &view->buttons[index];
    button->label = label;
    button->primary = primary;
    button->enabled = enabled;
    ui_button_set_bounds(button, bounds);
}

void net_view_init(NetView *view)
{
    if (view == NULL) {
        return;
    }
    memset(view, 0, sizeof(*view));
    snprintf(view->host, sizeof(view->host), "127.0.0.1");
    snprintf(view->port, sizeof(view->port), "8080");
    view->color = STONE_BLACK;
    view->focus = -1;
    for (int i = 0; i < NET_BTN_COUNT; ++i) {
        ui_button_init(&view->buttons[i], "", false);
    }
}

static void fields_for_view(NetView *view, NetField fields[3])
{
    const float width = (float)GetScreenWidth();
    const float height = (float)GetScreenHeight();
    const float panel_width = fminf(560.0f, width - 40.0f);
    const float x = (width - panel_width) * 0.5f;
    const float top = fmaxf(80.0f, height * 0.16f);
    fields[0] = (NetField){{x + 120.0f, top, panel_width - 130.0f, 36.0f},
                           "Server", view->host, sizeof(view->host), false};
    fields[1] = (NetField){{x + 120.0f, top + 46.0f, 110.0f, 36.0f},
                           "Port", view->port, sizeof(view->port), true};
    fields[2] = (NetField){{x + 120.0f, top + 92.0f, 150.0f, 36.0f},
                           "Room code", view->code, sizeof(view->code), false};
}

static void edit_field(NetView *view, NetField *field)
{
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressed(KEY_DELETE)) {
        const size_t length = strlen(field->value);
        if (length > 0) {
            field->value[length - 1] = '\0';
        }
    }
    for (int c = GetCharPressed(); c > 0; c = GetCharPressed()) {
        if (field->numeric && !isdigit(c)) {
            continue;
        }
        if (!field->numeric && !(isalnum(c) || c == '.' || c == '-')) {
            continue;
        }
        const size_t length = strlen(field->value);
        if (length + 1 < field->capacity) {
            field->value[length] = (char)toupper(c);
            field->value[length + 1] = '\0';
        }
    }
    if (IsKeyPressed(KEY_TAB)) {
        view->focus = (view->focus + 1) % 3;
    }
    if (IsKeyPressed(KEY_ENTER)) {
        view->focus = -1;
    }
}

NetViewAction net_view_update(NetView *view, const UiInput *input, float dt,
                              const NetSession *session)
{
    if (view == NULL || input == NULL || session == NULL) {
        return NET_VIEW_NONE;
    }
    view->caret += dt;
    NetField fields[3];
    fields_for_view((NetView *)view, fields);

    if (input->mouse_pressed) {
        for (int i = 0; i < 3; ++i) {
            if (CheckCollisionPointRec(input->mouse, fields[i].bounds)) {
                view->focus = i;
                break;
            }
        }
    }
    if (view->focus >= 0 && view->focus < 3) {
        edit_field(view, &fields[view->focus]);
    }

    const float width = (float)GetScreenWidth();
    const float height = (float)GetScreenHeight();
    const float panel_width = fminf(560.0f, width - 40.0f);
    const float x = (width - panel_width) * 0.5f;
    const float top = fmaxf(80.0f, height * 0.16f);
    const float gap = 8.0f;
    const float button_width = (panel_width - gap * 2.0f) / 3.0f;
    const float button_y = top + 240.0f;

    const bool connected = session->phase == NET_SESSION_CONNECTING
        || session->phase == NET_SESSION_LOBBY
        || session->phase == NET_SESSION_WAITING
        || session->phase == NET_SESSION_PLAYING
        || session->phase == NET_SESSION_FINISHED;
    const bool in_room = session->code[0] != '\0';

    set_button(view, NET_BTN_BACK, "Back", false,
               (Rectangle){x, button_y + 132.0f, button_width * 2.0f + gap,
                           38.0f}, true);
    set_button(view, NET_BTN_CONNECT, connected ? "Disconnect" : "Connect",
               !connected,
               (Rectangle){x, button_y, button_width, 38.0f},
               session->phase != NET_SESSION_CONNECTING);
    set_button(view, NET_BTN_COLOR,
               view->color == STONE_BLACK ? "Play as Black" : "Play as White",
               false, (Rectangle){x + button_width + gap, button_y,
                                  button_width, 38.0f}, !in_room);
    set_button(view, NET_BTN_CREATE, "Create room", false,
               (Rectangle){x + (button_width + gap) * 2.0f, button_y,
                           button_width, 38.0f},
               session->phase == NET_SESSION_LOBBY && !in_room);
    set_button(view, NET_BTN_JOIN, "Join room", false,
               (Rectangle){x, button_y + 46.0f, button_width, 38.0f},
               session->phase == NET_SESSION_LOBBY && !in_room);
    set_button(view, NET_BTN_READY, "Ready",
               true, (Rectangle){x + button_width + gap, button_y + 46.0f,
                                 button_width, 38.0f},
               session->phase == NET_SESSION_WAITING && !session->ready);
    set_button(view, NET_BTN_LEAVE, "Leave room", false,
               (Rectangle){x + (button_width + gap) * 2.0f, button_y + 46.0f,
                           button_width, 38.0f}, in_room);
    set_button(view, NET_BTN_COPY, "Copy code", false,
               (Rectangle){x + button_width * 2.0f + gap * 2.0f,
                           button_y + 132.0f, button_width, 38.0f},
               in_room);

    NetViewAction action = NET_VIEW_NONE;
    for (int i = 0; i < NET_BTN_COUNT; ++i) {
        if (view->buttons[i].enabled
            && ui_button_update(&view->buttons[i], input, true, dt)) {
            switch (i) {
            case NET_BTN_BACK: action = NET_VIEW_BACK; break;
            case NET_BTN_CONNECT:
                action = connected ? NET_VIEW_DISCONNECT : NET_VIEW_CONNECT;
                break;
            case NET_BTN_COLOR: action = NET_VIEW_TOGGLE_COLOR; break;
            case NET_BTN_CREATE: action = NET_VIEW_CREATE; break;
            case NET_BTN_JOIN: action = NET_VIEW_JOIN; break;
            case NET_BTN_READY: action = NET_VIEW_READY; break;
            case NET_BTN_LEAVE: action = NET_VIEW_LEAVE; break;
            case NET_BTN_COPY: action = NET_VIEW_COPY_CODE; break;
            default: break;
            }
        }
    }
    if (input->escape_pressed) {
        action = NET_VIEW_BACK;
    }
    return action;
}

void net_view_draw(const NetView *view, const NetSession *session,
                   const UiFonts *fonts)
{
    if (view == NULL || session == NULL || fonts == NULL) {
        return;
    }
    ui_draw_background();
    const float width = (float)GetScreenWidth();
    const float height = (float)GetScreenHeight();
    const float panel_width = fminf(560.0f, width - 40.0f);
    const float x = (width - panel_width) * 0.5f;
    const float top = fmaxf(80.0f, height * 0.16f);
    const Rectangle panel = {x - 20.0f, top - 26.0f,
                             panel_width + 40.0f,
                             400.0f};
    ui_draw_panel(panel, REVERSI_UI_RADIUS_LG, REVERSI_UI_COLOR_SURFACE,
                  REVERSI_UI_COLOR_BORDER);
    ui_draw_text_clipped(fonts, UI_TEXT_SEMIBOLD, "Online play",
                         (Rectangle){x, top - 18.0f, panel_width, 30.0f},
                         REVERSI_UI_FONT_HEADING, REVERSI_UI_COLOR_TEXT,
                         UI_ALIGN_LEFT);

    NetField fields[3];
    fields_for_view((NetView *)view, fields);
    for (int i = 0; i < 3; ++i) {
        ui_draw_text_clipped(fonts, UI_TEXT_REGULAR, fields[i].label,
                             (Rectangle){x, fields[i].bounds.y,
                                         fields[i].bounds.x - x - 8.0f, 36.0f},
                             REVERSI_UI_FONT_SMALL,
                             REVERSI_UI_COLOR_TEXT_MUTED, UI_ALIGN_LEFT);
        ui_draw_panel(fields[i].bounds, REVERSI_UI_RADIUS_SM,
                      REVERSI_UI_COLOR_SURFACE_ALT,
                      view->focus == i ? REVERSI_UI_COLOR_ACCENT
                                       : REVERSI_UI_COLOR_BORDER);
        ui_draw_text_clipped(fonts, UI_TEXT_REGULAR, fields[i].value,
                             (Rectangle){fields[i].bounds.x + 10.0f,
                                         fields[i].bounds.y,
                                         fields[i].bounds.width - 20.0f,
                                         fields[i].bounds.height},
                             REVERSI_UI_FONT_BODY, REVERSI_UI_COLOR_TEXT,
                             UI_ALIGN_LEFT);
    }

    char status[192];
    snprintf(status, sizeof(status), "%s",
             net_session_status_text(session));
    if (session->latency_ms > 0) {
        const size_t used = strlen(status);
        snprintf(status + used, sizeof(status) - used, "   Ping %d ms",
                 session->latency_ms);
    }
    ui_draw_text_clipped(fonts, UI_TEXT_SEMIBOLD, status,
                         (Rectangle){x, top + 140.0f, panel_width, 20.0f},
                         REVERSI_UI_FONT_SMALL, REVERSI_UI_COLOR_TEXT,
                         UI_ALIGN_LEFT);
    if (session->code[0] != '\0') {
        char room[128];
        snprintf(room, sizeof(room), "Room %s   You: %s%s", session->code,
                 ui_player_name(session->color),
                 session->is_host ? " (host)" : "");
        ui_draw_text_clipped(fonts, UI_TEXT_REGULAR, room,
                             (Rectangle){x, top + 162.0f, panel_width,
                                         20.0f},
                             REVERSI_UI_FONT_SMALL,
                             REVERSI_UI_COLOR_TEXT_MUTED, UI_ALIGN_LEFT);
    }
    if (session->opponent_status[0] != '\0') {
        char opponent[128];
        snprintf(opponent, sizeof(opponent), "Opponent: %s",
                 session->opponent_status);
        ui_draw_text_clipped(fonts, UI_TEXT_REGULAR, opponent,
                             (Rectangle){x, top + 182.0f, panel_width,
                                         20.0f},
                             REVERSI_UI_FONT_TINY,
                             REVERSI_UI_COLOR_TEXT_FAINT, UI_ALIGN_LEFT);
    }
    if (session->reason[0] != '\0') {
        ui_draw_text_clipped(fonts, UI_TEXT_REGULAR, session->reason,
                             (Rectangle){x, top + 202.0f, panel_width,
                                         20.0f},
                             REVERSI_UI_FONT_TINY,
                             REVERSI_UI_COLOR_TEXT_FAINT, UI_ALIGN_LEFT);
    }
    if (session->error[0] != '\0') {
        ui_draw_text_clipped(fonts, UI_TEXT_REGULAR, session->error,
                             (Rectangle){x, top + 222.0f, panel_width,
                                         20.0f},
                             REVERSI_UI_FONT_TINY, REVERSI_UI_COLOR_DANGER,
                             UI_ALIGN_LEFT);
    }

    for (int i = 0; i < NET_BTN_COUNT; ++i) {
        if (view->buttons[i].enabled) {
            ui_button_draw(&view->buttons[i], fonts, false);
        }
    }
    ui_draw_text_clipped(
        fonts, UI_TEXT_REGULAR,
        "Share the room code; both players press Ready to start.",
        (Rectangle){x, top + 260.0f, panel_width, 18.0f},
        REVERSI_UI_FONT_TINY, REVERSI_UI_COLOR_TEXT_FAINT, UI_ALIGN_LEFT);
}
