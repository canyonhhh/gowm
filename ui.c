#include "ui.h"

#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <string.h>

static void draw_indicator(Ui *ui, const WorkspaceState *state)
{
    Indicator *indicator = &ui->indicator;
    if (indicator->window == None || !indicator->gc)
        return;

    char label[2] = {(char)('1' + state->focused), '\0'};
    XClearWindow(ui->dpy, indicator->window);
    XDrawString(ui->dpy, indicator->window, indicator->gc, 4, 12, label, 1);
}

static void create_indicator(Ui *ui)
{
    Display *dpy = ui->dpy;
    Indicator *indicator = &ui->indicator;
    int screen = DefaultScreen(dpy);
    XSetWindowAttributes attrs = {0};
    attrs.override_redirect = True;
    attrs.background_pixel = BlackPixel(dpy, screen);
    attrs.border_pixel = BlackPixel(dpy, screen);

    indicator->window = XCreateWindow(
        dpy, ui->root, 4, 4, 16, 16, 0,
        CopyFromParent, InputOutput, CopyFromParent,
        CWOverrideRedirect | CWBackPixel | CWBorderPixel, &attrs);
    XSelectInput(dpy, indicator->window, ExposureMask);
    indicator->gc = XCreateGC(dpy, indicator->window, 0, NULL);
    XSetForeground(dpy, indicator->gc, WhitePixel(dpy, screen));
    indicator->font = XLoadQueryFont(dpy, "fixed");
    if (indicator->font)
        XSetFont(dpy, indicator->gc, indicator->font->fid);
    XMapRaised(dpy, indicator->window);
}

static void create_overview(Ui *ui)
{
    Display *dpy = ui->dpy;
    Overview *overview = &ui->overview;
    int screen = DefaultScreen(dpy);
    unsigned long white = WhitePixel(dpy, screen);
    overview->text = (XftColor){white, {65535, 65535, 65535, 65535}};

    XSetWindowAttributes attrs = {0};
    attrs.override_redirect = True;
    attrs.background_pixel = BlackPixel(dpy, screen);
    attrs.event_mask = ExposureMask | KeyPressMask;
    overview->window = XCreateWindow(
        dpy, ui->root, 0, 0, 1, 1, 0,
        CopyFromParent, InputOutput, CopyFromParent,
        CWOverrideRedirect | CWBackPixel | CWEventMask, &attrs);
    XStoreName(dpy, overview->window, "gowm workspace overview");
    overview->gc = XCreateGC(dpy, overview->window, 0, NULL);
    overview->draw = XftDrawCreate(dpy, overview->window,
                                   DefaultVisual(dpy, screen),
                                   DefaultColormap(dpy, screen));
    overview->font = XftFontOpenName(dpy, screen, "sans:pixelsize=18");
    if (!overview->font || !overview->draw)
        fprintf(stderr, "cannot initialize workspace overview text rendering\n");
}

void ui_init(Ui *ui, Display *dpy, Window root)
{
    *ui = (Ui){.dpy = dpy, .root = root};
    ui->net_wm_name = XInternAtom(dpy, "_NET_WM_NAME", False);
    ui->utf8_string = XInternAtom(dpy, "UTF8_STRING", False);
    create_indicator(ui);
    create_overview(ui);
}

static int text_width(Ui *ui, const char *text, int length)
{
    XGlyphInfo extents;
    XftTextExtentsUtf8(ui->dpy, ui->overview.font,
                      (const FcChar8 *)text, length, &extents);
    return extents.xOff;
}

/* Fit labels by pixel width, removing whole UTF-8 characters when shortening. */
static void draw_label(Ui *ui, int x, int y, int width, const char *text)
{
    Overview *overview = &ui->overview;
    if (width <= 0 || !overview->font || !overview->draw)
        return;
    char label[1024];
    size_t bytes = strlen(text);
    if (bytes > sizeof(label) - 4) {
        bytes = sizeof(label) - 4;
        while (bytes > 0 && ((unsigned char)text[bytes] & 0xc0) == 0x80)
            bytes--;
    }
    memcpy(label, text, bytes);
    label[bytes] = '\0';
    for (char *p = label; *p; p++) {
        if ((unsigned char)*p < 32 || *p == 127)
            *p = ' ';
    }
    int len = (int)strlen(label);
    int dots = text_width(ui, "...", 3);
    if (text_width(ui, label, len) > width) {
        if (width < dots)
            return;
        do {
            do { len--; } while (len > 0 && (label[len] & 0xc0) == 0x80);
        } while (len > 0 && text_width(ui, label, len) + dots > width);
        memcpy(label + len, "...", 4);
        len += 3;
    }
    XftDrawStringUtf8(overview->draw, &overview->text, overview->font,
                      x, y, (const FcChar8 *)label, len);
}

static void window_title(Ui *ui, Window window, char *title, size_t size)
{
    Atom type;
    int format;
    unsigned long count, remaining;
    unsigned char *value = NULL;
    title[0] = '\0';
    if (XGetWindowProperty(ui->dpy, window, ui->net_wm_name, 0, 1024, False,
                           ui->utf8_string, &type, &format, &count, &remaining,
                           &value) == Success && value && type == ui->utf8_string &&
        format == 8 && count > 0 && value[0]) {
        snprintf(title, size, "%s", (char *)value);
        XFree(value);
        return;
    }
    if (value)
        XFree(value);

    XTextProperty property = {0};
    if (XGetWMName(ui->dpy, window, &property) && property.value) {
        char **list = NULL;
        int n = 0;
        if (Xutf8TextPropertyToTextList(ui->dpy, &property, &list, &n) >= Success &&
            list && n > 0 && list[0][0])
            snprintf(title, size, "%s", list[0]);
        if (list)
            XFreeStringList(list);
        XFree(property.value);
    }
}

void ui_draw_overview(Ui *ui, const WorkspaceState *state)
{
    Overview *overview = &ui->overview;
    Display *dpy = ui->dpy;
    if (!overview->visible || !overview->font || !overview->draw || !overview->gc)
        return;

    XClearWindow(dpy, overview->window);
    int margin = overview->width / 24;
    if (margin < 8) margin = 8;
    int gap = overview->width / 64;
    if (gap < 8) gap = 8;
    if (gap > 20) gap = 20;
    int width = overview->width - 2 * margin;
    if (width > 1320) width = 1320;
    int height = overview->height - 120;
    if (height > 810) height = 810;
    if (width < 90 || height < 90)
        return;
    int columns = OVERVIEW_COLUMNS;
    int rows = (NUM_WS + columns - 1) / columns;
    int left = (overview->width - width) / 2;
    int top = (overview->height - height) / 2;
    int card_w = (width - (columns - 1) * gap) / columns;
    int card_h = (height - (rows - 1) * gap) / rows;
    if (card_w < 4 || card_h < 4)
        return;

    XSetForeground(dpy, overview->gc, overview->text.pixel);
    for (int ws = 0; ws < NUM_WS; ws++) {
        int x = left + (ws % columns) * (card_w + gap);
        int y = top + (ws / columns) * (card_h + gap);
        XSetLineAttributes(dpy, overview->gc, ws == state->focused ? 3 : 1,
                           LineSolid, CapButt, JoinMiter);
        XDrawRectangle(dpy, overview->window, overview->gc, x + 1, y + 1,
                       (unsigned int)(card_w - 3), (unsigned int)(card_h - 3));
        XSetLineAttributes(dpy, overview->gc, 1, LineSolid, CapButt, JoinMiter);

        char number[2] = {(char)('1' + ws), '\0'};
        draw_label(ui, x + 16, y + 29, card_w - 32, number);
        if (state->windows[ws] == None || card_h < 85 || card_w < 64)
            continue;

        XClassHint app = {0};
        XGetClassHint(dpy, state->windows[ws], &app);
        const char *name = app.res_class && app.res_class[0] ? app.res_class :
                           app.res_name && app.res_name[0] ? app.res_name : "";
        int baseline = y + card_h / 2;
        draw_label(ui, x + 16, baseline, card_w - 32, name);
        char title[1024];
        window_title(ui, state->windows[ws], title, sizeof(title));
        draw_label(ui, x + 16, baseline + 25, card_w - 32, title);
        if (app.res_name) XFree(app.res_name);
        if (app.res_class) XFree(app.res_class);
    }
}

static const MonitorGeometry *focused_monitor(const WorkspaceState *state,
                                              const MonitorSet *monitors)
{
    int monitor = workspace_monitor(state, state->focused);
    if (monitor < 0 || monitor >= monitors->count || monitor >= NUM_MONITORS)
        return NULL;
    const MonitorGeometry *geometry = &monitors->geometry[monitor];
    if (geometry->w <= 0 || geometry->h <= 0)
        return NULL;
    return geometry;
}

void ui_update(Ui *ui, const WorkspaceState *state, const MonitorSet *monitors)
{
    const MonitorGeometry *mon = focused_monitor(state, monitors);
    if (!mon)
        return;

    if (ui->indicator.window != None) {
        XMoveWindow(ui->dpy, ui->indicator.window, mon->x + 4, mon->y + mon->h - 18);
        XRaiseWindow(ui->dpy, ui->indicator.window);
        draw_indicator(ui, state);
    }
    Overview *overview = &ui->overview;
    if (overview->visible) {
        overview->width = mon->w;
        overview->height = mon->h;
        XMoveResizeWindow(ui->dpy, overview->window, mon->x, mon->y,
                          (unsigned int)mon->w, (unsigned int)mon->h);
        XRaiseWindow(ui->dpy, overview->window);
        ui_draw_overview(ui, state);
    }
}

void ui_expose(Ui *ui, const XExposeEvent *event, const WorkspaceState *state)
{
    if (event->window == ui->indicator.window)
        draw_indicator(ui, state);
    else if (event->window == ui->overview.window && event->count == 0)
        ui_draw_overview(ui, state);
}

bool ui_show_overview(Ui *ui, const WorkspaceState *state, const MonitorSet *monitors)
{
    Overview *overview = &ui->overview;
    Display *dpy = ui->dpy;
    if (overview->visible)
        return true;
    if (overview->window == None || !overview->font || !overview->draw ||
        !overview->gc || !focused_monitor(state, monitors))
        return false;

    overview->visible = true;
    ui_update(ui, state, monitors);
    XMapRaised(dpy, overview->window);
    /* owner_events=False routes even unmodified keys to this modal window. */
    if (XGrabKeyboard(dpy, overview->window, False, GrabModeAsync, GrabModeAsync,
                      CurrentTime) != GrabSuccess) {
        overview->visible = false;
        XUnmapWindow(dpy, overview->window);
        return false;
    }
    /* Don't let clicks reach applications on another monitor while choosing. */
    if (XGrabPointer(dpy, overview->window, False, ButtonPressMask | ButtonReleaseMask,
                     GrabModeAsync, GrabModeAsync, None, None, CurrentTime) != GrabSuccess) {
        XUngrabKeyboard(dpy, CurrentTime);
        overview->visible = false;
        XUnmapWindow(dpy, overview->window);
        return false;
    }
    XSetInputFocus(dpy, overview->window, RevertToPointerRoot, CurrentTime);
    return true;
}

void ui_hide_overview(Ui *ui)
{
    if (!ui->overview.visible)
        return;
    ui->overview.visible = false;
    XUngrabKeyboard(ui->dpy, CurrentTime);
    XUngrabPointer(ui->dpy, CurrentTime);
    XUnmapWindow(ui->dpy, ui->overview.window);
}

bool ui_owns_window(const Ui *ui, Window window)
{
    return window != None &&
           (window == ui->indicator.window || window == ui->overview.window);
}

bool ui_title_property(const Ui *ui, Atom atom)
{
    return atom != None &&
           (atom == ui->net_wm_name || atom == XA_WM_NAME || atom == XA_WM_CLASS);
}

void ui_destroy(Ui *ui)
{
    if (!ui->dpy)
        return;
    ui_hide_overview(ui);
    if (ui->overview.font)
        XftFontClose(ui->dpy, ui->overview.font);
    if (ui->overview.draw)
        XftDrawDestroy(ui->overview.draw);
    if (ui->overview.gc)
        XFreeGC(ui->dpy, ui->overview.gc);
    if (ui->overview.window != None)
        XDestroyWindow(ui->dpy, ui->overview.window);
    if (ui->indicator.font)
        XFreeFont(ui->dpy, ui->indicator.font);
    if (ui->indicator.gc)
        XFreeGC(ui->dpy, ui->indicator.gc);
    if (ui->indicator.window != None)
        XDestroyWindow(ui->dpy, ui->indicator.window);
    *ui = (Ui){0};
}
