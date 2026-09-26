#ifndef GOWM_BINDINGS_H
#define GOWM_BINDINGS_H

#include <X11/Xlib.h>
#include <X11/keysym.h>

typedef enum {
    ACTION_OVERVIEW,
    ACTION_CLOSE,
    ACTION_QUIT,
    ACTION_SWITCH,
    ACTION_MOVE
} Action;

typedef struct {
    KeySym key;
    unsigned int modifiers;
    Action action;
    int argument;
} Binding;

/* This table drives both passive grabs and key dispatch. Use unshifted keysyms. */
#define WORKSPACE_BINDINGS(n) \
    { XK_1 + (n) - 1, ControlMask, ACTION_SWITCH, (n) - 1 }, \
    { XK_1 + (n) - 1, ControlMask | ShiftMask, ACTION_MOVE, (n) - 1 }

static const Binding bindings[] = {
    { XK_w, ControlMask, ACTION_OVERVIEW, 0 },
    { XK_q, ControlMask | ShiftMask, ACTION_CLOSE, 0 },
    { XK_c, ControlMask | ShiftMask, ACTION_QUIT, 0 },
    WORKSPACE_BINDINGS(1),
    WORKSPACE_BINDINGS(2),
    WORKSPACE_BINDINGS(3),
    WORKSPACE_BINDINGS(4),
    WORKSPACE_BINDINGS(5),
    WORKSPACE_BINDINGS(6),
    WORKSPACE_BINDINGS(7),
    WORKSPACE_BINDINGS(8),
    WORKSPACE_BINDINGS(9),
};
#undef WORKSPACE_BINDINGS

#endif
