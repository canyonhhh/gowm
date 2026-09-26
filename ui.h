#ifndef GOWM_UI_H
#define GOWM_UI_H

#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <stdbool.h>
#include "workspace.h"
#include "monitors.h"

typedef struct {
    Window window;
    GC gc;
    XFontStruct *font;
} Indicator;

typedef struct {
    Window window;
    GC gc;
    XftDraw *draw;
    XftFont *font;
    XftColor text;
    bool visible;
    int width, height;
} Overview;

typedef struct {
    Display *dpy;
    Window root;
    Indicator indicator;
    Overview overview;
    Atom net_wm_name, utf8_string;
} Ui;

void ui_init(Ui *ui, Display *dpy, Window root);
void ui_destroy(Ui *ui);
void ui_update(Ui *ui, const WorkspaceState *state, const MonitorSet *monitors);
void ui_draw_overview(Ui *ui, const WorkspaceState *state);
void ui_expose(Ui *ui, const XExposeEvent *event, const WorkspaceState *state);
bool ui_show_overview(Ui *ui, const WorkspaceState *state, const MonitorSet *monitors);
/* Releases grabs and hides the overview; the controller restores focus. */
void ui_hide_overview(Ui *ui);
bool ui_owns_window(const Ui *ui, Window window);
bool ui_title_property(const Ui *ui, Atom atom);

#endif
