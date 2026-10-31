#ifndef REVERSI_UI_NET_VIEW_H
#define REVERSI_UI_NET_VIEW_H

#include "net/net_session.h"
#include "ui/draw.h"

#include <raylib.h>
#include <stdbool.h>

typedef enum NetViewAction {
    NET_VIEW_NONE = 0,
    NET_VIEW_BACK,
    NET_VIEW_CONNECT,
    NET_VIEW_DISCONNECT,
    NET_VIEW_TOGGLE_COLOR,
    NET_VIEW_CREATE,
    NET_VIEW_JOIN,
    NET_VIEW_READY,
    NET_VIEW_LEAVE,
    NET_VIEW_COPY_CODE
} NetViewAction;

typedef enum NetViewButton {
    NET_BTN_BACK = 0,
    NET_BTN_CONNECT,
    NET_BTN_COLOR,
    NET_BTN_CREATE,
    NET_BTN_JOIN,
    NET_BTN_READY,
    NET_BTN_LEAVE,
    NET_BTN_COPY,
    NET_BTN_COUNT
} NetViewButton;

typedef struct NetView {
    char host[64];
    char port[8];
    char code[REVERSI_NET_CODE_LEN + 1];
    int focus;
    Stone color;
    bool use_tls;
    float caret;
    UiButton buttons[NET_BTN_COUNT];
} NetView;

void net_view_init(NetView *view);
NetViewAction net_view_update(NetView *view, const UiInput *input, float dt,
                              const NetSession *session);
void net_view_draw(const NetView *view, const NetSession *session,
                   const UiFonts *fonts);

#endif
