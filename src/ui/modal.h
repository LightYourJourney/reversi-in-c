#ifndef REVERSI_UI_MODAL_H
#define REVERSI_UI_MODAL_H

#include "ui/draw.h"

typedef enum ModalPurpose {
    MODAL_PURPOSE_NONE = 0,
    MODAL_PURPOSE_QUIT,
    MODAL_PURPOSE_RESTART,
    MODAL_PURPOSE_RETURN_MENU
} ModalPurpose;

typedef enum ModalAction {
    MODAL_ACTION_NONE = 0,
    MODAL_ACTION_CONFIRM,
    MODAL_ACTION_CANCEL
} ModalAction;

typedef struct Modal {
    ModalPurpose purpose;
    UiButton confirm;
    UiButton cancel;
    float appear;
} Modal;

void modal_init(Modal *modal);
bool modal_is_open(const Modal *modal);
void modal_open(Modal *modal, ModalPurpose purpose);
void modal_close(Modal *modal);
ModalAction modal_update(Modal *modal, const UiInput *input, float dt);
void modal_draw(const Modal *modal, const UiFonts *fonts);

#endif
