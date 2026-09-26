#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
#include <X11/keysym.h>
#include <X11/extensions/Xinerama.h>
#include <X11/extensions/Xrandr.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#define NUM_WS 9
#define NUM_MONITORS 2
#define MAIN_OUTPUT_NAME "eDP-1"

static int running = 1;
static Window workspaces[NUM_WS];
static Window ws_indicator = None;
static GC ws_indicator_gc = 0;
static XFontStruct *ws_indicator_font = NULL;

static Window overview = None;
static GC overview_gc = 0;
static XftDraw *overview_draw = NULL;
static XftFont *overview_font = NULL;
static int overview_visible = 0;
static int overview_width, overview_height;
static XftColor overview_text;
static Atom net_wm_name, utf8_string;

static void update_overview(Display *dpy);

/* monitor 0 = main, monitor 1 = external (when connected) */
static int monitor_count = 1;
static int active_ws[NUM_MONITORS] = {0, 0};
static int focused_ws = 0;

struct monitor_geo {
    int x;
    int y;
    int w;
    int h;
};

static struct monitor_geo monitors[NUM_MONITORS];

static int xerror(Display *dpy, XErrorEvent *ee)
{
    (void)dpy;
    fprintf(stderr, "X error: request=%d error=%d\n",
            ee->request_code, ee->error_code);
    return 0;
}

static int is_external_workspace(int ws)
{
    return ws >= 0 && ws <= 5;
}

static int workspace_monitor(int ws)
{
    if (monitor_count >= 2 && is_external_workspace(ws))
        return 1;
    return 0;
}

static int monitor_at(int x, int y)
{
    for (int i = 0; i < monitor_count; i++) {
        if (x >= monitors[i].x && x < monitors[i].x + monitors[i].w &&
            y >= monitors[i].y && y < monitors[i].y + monitors[i].h)
            return i;
    }
    return -1;
}

static void focus_window(Display *dpy, Window w)
{
    if (overview_visible) {
        XRaiseWindow(dpy, overview);
        return;
    }
    if (w == None) {
        XSetInputFocus(dpy, DefaultRootWindow(dpy), RevertToPointerRoot, CurrentTime);
        return;
    }
    XMapRaised(dpy, w);
    XSetInputFocus(dpy, w, RevertToPointerRoot, CurrentTime);
    if (ws_indicator != None)
        XRaiseWindow(dpy, ws_indicator);
}

static void close_window(Display *dpy, Window w)
{
    if (w == None) return;

    Atom wm_protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
    Atom wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    Atom *protocols = NULL;
    int count = 0;

    if (XGetWMProtocols(dpy, w, &protocols, &count)) {
        for (int i = 0; i < count; i++) {
            if (protocols[i] == wm_delete) {
                XEvent ev = {0};
                ev.xclient.type = ClientMessage;
                ev.xclient.window = w;
                ev.xclient.message_type = wm_protocols;
                ev.xclient.format = 32;
                ev.xclient.data.l[0] = wm_delete;
                ev.xclient.data.l[1] = CurrentTime;
                XSendEvent(dpy, w, False, NoEventMask, &ev);
                XFree(protocols);
                return;
            }
        }
        XFree(protocols);
    }

    XDestroyWindow(dpy, w);
}

static int find_workspace_by_window(Window w)
{
    for (int i = 0; i < NUM_WS; i++) {
        if (workspaces[i] == w)
            return i;
    }
    return -1;
}

static int find_empty_ws_for_monitor(int monitor)
{
    for (int i = 0; i < NUM_WS; i++) {
        if (workspace_monitor(i) == monitor && workspaces[i] == None)
            return i;
    }
    return -1;
}

static int overlap_area(int ax, int ay, int aw, int ah,
                        int bx, int by, int bw, int bh)
{
    int x1 = (ax > bx) ? ax : bx;
    int y1 = (ay > by) ? ay : by;
    int x2 = ((ax + aw) < (bx + bw)) ? (ax + aw) : (bx + bw);
    int y2 = ((ay + ah) < (by + bh)) ? (ay + ah) : (by + bh);

    if (x2 <= x1 || y2 <= y1)
        return 0;
    return (x2 - x1) * (y2 - y1);
}

static int output_name_is_internal(const char *name)
{
    return strncmp(name, "eDP", 3) == 0 ||
           strncmp(name, "LVDS", 4) == 0 ||
           strncmp(name, "DSI", 3) == 0;
}

static void refresh_monitors(Display *dpy)
{
    int screen = DefaultScreen(dpy);
    int sw = DisplayWidth(dpy, screen);
    int sh = DisplayHeight(dpy, screen);

    monitor_count = 1;
    monitors[0].x = 0;
    monitors[0].y = 0;
    monitors[0].w = sw;
    monitors[0].h = sh;

    if (!XineramaIsActive(dpy))
        return;

    int n = 0;
    XineramaScreenInfo *info = XineramaQueryScreens(dpy, &n);
    if (!info || n < 2) {
        if (info) XFree(info);
        return;
    }

    int main_idx = 0;
    int ext_idx = 1;

    int main_x = 0, main_y = 0, main_w = 0, main_h = 0;
    int have_main_rect = 0;

    Window root = RootWindow(dpy, screen);
    XRRScreenResources *res = XRRGetScreenResourcesCurrent(dpy, root);
    if (res) {
        const char *main_name = MAIN_OUTPUT_NAME;
        RROutput primary = XRRGetOutputPrimary(dpy, root);
        RROutput internal_candidate = None;

        for (int i = 0; i < res->noutput; i++) {
            RROutput out_id = res->outputs[i];
            XRROutputInfo *out = XRRGetOutputInfo(dpy, res, out_id);
            if (!out)
                continue;

            if (out->connection != RR_Connected || out->crtc == None) {
                XRRFreeOutputInfo(out);
                continue;
            }

            if (main_name && strcmp(main_name, out->name) == 0) {
                XRRCrtcInfo *crtc = XRRGetCrtcInfo(dpy, res, out->crtc);
                if (crtc) {
                    main_x = crtc->x;
                    main_y = crtc->y;
                    main_w = (int)crtc->width;
                    main_h = (int)crtc->height;
                    have_main_rect = 1;
                    XRRFreeCrtcInfo(crtc);
                }
                XRRFreeOutputInfo(out);
                break;
            }

            if (!have_main_rect && out_id == primary) {
                XRRCrtcInfo *crtc = XRRGetCrtcInfo(dpy, res, out->crtc);
                if (crtc) {
                    main_x = crtc->x;
                    main_y = crtc->y;
                    main_w = (int)crtc->width;
                    main_h = (int)crtc->height;
                    have_main_rect = 1;
                    XRRFreeCrtcInfo(crtc);
                }
            }

            if (internal_candidate == None && output_name_is_internal(out->name))
                internal_candidate = out_id;

            XRRFreeOutputInfo(out);
        }

        if (!have_main_rect && internal_candidate != None) {
            XRROutputInfo *out = XRRGetOutputInfo(dpy, res, internal_candidate);
            if (out && out->connection == RR_Connected && out->crtc != None) {
                XRRCrtcInfo *crtc = XRRGetCrtcInfo(dpy, res, out->crtc);
                if (crtc) {
                    main_x = crtc->x;
                    main_y = crtc->y;
                    main_w = (int)crtc->width;
                    main_h = (int)crtc->height;
                    have_main_rect = 1;
                    XRRFreeCrtcInfo(crtc);
                }
            }
            if (out)
                XRRFreeOutputInfo(out);
        }

        XRRFreeScreenResources(res);
    }

    if (have_main_rect) {
        int best_overlap = -1;
        for (int i = 0; i < n; i++) {
            int ov = overlap_area(info[i].x_org, info[i].y_org,
                                  info[i].width, info[i].height,
                                  main_x, main_y, main_w, main_h);
            if (ov > best_overlap) {
                best_overlap = ov;
                main_idx = i;
            }
        }
    }

    for (int i = 0; i < n; i++) {
        if (i != main_idx) {
            ext_idx = i;
            break;
        }
    }

    if (ext_idx >= 0) {
        monitor_count = 2;
        monitors[0].x = info[main_idx].x_org;
        monitors[0].y = info[main_idx].y_org;
        monitors[0].w = info[main_idx].width;
        monitors[0].h = info[main_idx].height;

        monitors[1].x = info[ext_idx].x_org;
        monitors[1].y = info[ext_idx].y_org;
        monitors[1].w = info[ext_idx].width;
        monitors[1].h = info[ext_idx].height;
    }

    XFree(info);
}

static void ensure_active_workspaces(void)
{
    if (monitor_count >= 2) {
        if (workspace_monitor(active_ws[0]) != 0)
            active_ws[0] = 6;
        if (workspace_monitor(active_ws[1]) != 1)
            active_ws[1] = 0;
    } else {
        active_ws[0] = focused_ws;
    }
}

static void draw_ws_indicator(Display *dpy)
{
    if (ws_indicator == None || ws_indicator_gc == 0)
        return;

    char label[2];
    label[0] = (char)('1' + focused_ws);
    label[1] = '\0';

    XClearWindow(dpy, ws_indicator);
    XDrawString(dpy, ws_indicator, ws_indicator_gc, 4, 12, label, 1);
}

static void update_ws_indicator(Display *dpy)
{
    if (ws_indicator == None)
        return;

    int mon = workspace_monitor(focused_ws);
    int x = monitors[mon].x + 4;
    int y = monitors[mon].y + monitors[mon].h - 18;

    XMoveWindow(dpy, ws_indicator, x, y);
    XRaiseWindow(dpy, ws_indicator);
    draw_ws_indicator(dpy);
}

static void create_ws_indicator(Display *dpy, Window root)
{
    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    attrs.background_pixel = BlackPixel(dpy, DefaultScreen(dpy));
    attrs.border_pixel = BlackPixel(dpy, DefaultScreen(dpy));

    ws_indicator = XCreateWindow(
        dpy, root,
        4, 4, 16, 16, 0,
        CopyFromParent, InputOutput, CopyFromParent,
        CWOverrideRedirect | CWBackPixel | CWBorderPixel,
        &attrs);

    XSelectInput(dpy, ws_indicator, ExposureMask);
    XMapRaised(dpy, ws_indicator);

    ws_indicator_gc = XCreateGC(dpy, ws_indicator, 0, NULL);
    XSetForeground(dpy, ws_indicator_gc, WhitePixel(dpy, DefaultScreen(dpy)));
    ws_indicator_font = XLoadQueryFont(dpy, "fixed");
    if (ws_indicator_font)
        XSetFont(dpy, ws_indicator_gc, ws_indicator_font->fid);

    update_ws_indicator(dpy);
}

static int sync_monitors(Display *dpy)
{
    int prev_count = monitor_count;
    struct monitor_geo prev_monitors[NUM_MONITORS] = { monitors[0], monitors[1] };
    int prev_active_ws[NUM_MONITORS] = { active_ws[0], active_ws[1] };

    refresh_monitors(dpy);
    ensure_active_workspaces();

    if (monitor_count != prev_count)
        return 1;

    for (int i = 0; i < NUM_MONITORS; i++) {
        if (monitors[i].x != prev_monitors[i].x ||
            monitors[i].y != prev_monitors[i].y ||
            monitors[i].w != prev_monitors[i].w ||
            monitors[i].h != prev_monitors[i].h)
            return 1;
    }

    if (active_ws[0] != prev_active_ws[0] || active_ws[1] != prev_active_ws[1])
        return 1;

    return 0;
}

static void apply_layout(Display *dpy)
{
    for (int ws = 0; ws < NUM_WS; ws++) {
        Window w = workspaces[ws];
        if (w == None)
            continue;

        int mon = workspace_monitor(ws);
        if (active_ws[mon] == ws) {
            XMoveResizeWindow(dpy, w,
                              monitors[mon].x,
                              monitors[mon].y,
                              (unsigned int)monitors[mon].w,
                              (unsigned int)monitors[mon].h);
            XMapWindow(dpy, w);
            XRaiseWindow(dpy, w);
        } else {
            XUnmapWindow(dpy, w);
        }
    }

    update_ws_indicator(dpy);
    update_overview(dpy);
}

static void center_pointer_on_monitor(Display *dpy, int mon)
{
    Window root = DefaultRootWindow(dpy);
    int cx = monitors[mon].x + monitors[mon].w / 2;
    int cy = monitors[mon].y + monitors[mon].h / 2;
    XWarpPointer(dpy, None, root, 0, 0, 0, 0, cx, cy);
}

static void focus_monitor(Display *dpy, int mon)
{
    if (overview_visible)
        return;
    if (monitor_count < 2)
        return;
    if (mon < 0 || mon >= monitor_count)
        return;
    if (mon == workspace_monitor(focused_ws))
        return;

    focused_ws = active_ws[mon];
    focus_window(dpy, workspaces[focused_ws]);
    update_ws_indicator(dpy);
}

static void switch_ws(Display *dpy, int target)
{
    int prev_mon = workspace_monitor(focused_ws);
    int mon = workspace_monitor(target);
    active_ws[mon] = target;
    focused_ws = target;

    apply_layout(dpy);
    focus_window(dpy, workspaces[target]);

    if (mon != prev_mon)
        center_pointer_on_monitor(dpy, mon);
}

static void move_window_to_ws(Display *dpy, int target)
{
    if (target == focused_ws) return;

    int src_ws = focused_ws;
    Window src = workspaces[src_ws];
    Window dst = workspaces[target];
    if (src == None) return;

    workspaces[src_ws] = dst;
    workspaces[target] = src;

    int src_mon = workspace_monitor(src_ws);
    int mon = workspace_monitor(target);
    active_ws[mon] = target;
    focused_ws = target;

    apply_layout(dpy);
    focus_window(dpy, src);

    if (dst != None)
        active_ws[src_mon] = src_ws;
}

static void create_overview(Display *dpy, Window root)
{
    unsigned long black = BlackPixel(dpy, DefaultScreen(dpy));
    unsigned long white = WhitePixel(dpy, DefaultScreen(dpy));
    overview_text = (XftColor){white, {65535, 65535, 65535, 65535}};

    XSetWindowAttributes attrs = {0};
    attrs.override_redirect = True;
    attrs.background_pixel = black;
    attrs.event_mask = ExposureMask | KeyPressMask;
    overview = XCreateWindow(dpy, root, 0, 0, 1, 1, 0,
                             CopyFromParent, InputOutput, CopyFromParent,
                             CWOverrideRedirect | CWBackPixel | CWEventMask, &attrs);
    XStoreName(dpy, overview, "gowm workspace overview");
    overview_gc = XCreateGC(dpy, overview, 0, NULL);
    int screen = DefaultScreen(dpy);
    overview_draw = XftDrawCreate(dpy, overview, DefaultVisual(dpy, screen),
                                  DefaultColormap(dpy, screen));
    overview_font = XftFontOpenName(dpy, screen, "sans:pixelsize=18");
    if (!overview_font || !overview_draw)
        fprintf(stderr, "cannot initialize workspace overview text rendering\n");
    net_wm_name = XInternAtom(dpy, "_NET_WM_NAME", False);
    utf8_string = XInternAtom(dpy, "UTF8_STRING", False);
}

static int overview_text_width(Display *dpy, const char *text, int length)
{
    XGlyphInfo extents;
    XftTextExtentsUtf8(dpy, overview_font, (const FcChar8 *)text, length, &extents);
    return extents.xOff;
}

/* Fit labels by pixel width, removing whole UTF-8 characters when shortening. */
static void overview_label(Display *dpy, int x, int y, int width,
                           const char *text)
{
    if (width <= 0 || !overview_font)
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
    int dots = overview_text_width(dpy, "...", 3);
    if (overview_text_width(dpy, label, len) > width) {
        if (width < dots)
            return;
        do {
            do { len--; } while (len > 0 && (label[len] & 0xc0) == 0x80);
        } while (len > 0 &&
                 overview_text_width(dpy, label, len) + dots > width);
        memcpy(label + len, "...", 4);
        len += 3;
    }
    XftDrawStringUtf8(overview_draw, &overview_text, overview_font,
                      x, y, (const FcChar8 *)label, len);
}

static void window_title(Display *dpy, Window w, char *title, size_t size)
{
    Atom type;
    int format;
    unsigned long count, remaining;
    unsigned char *value = NULL;
    title[0] = '\0';
    if (XGetWindowProperty(dpy, w, net_wm_name, 0, 1024, False,
                           utf8_string, &type, &format, &count, &remaining,
                           &value) == Success && value && type == utf8_string &&
        format == 8 && count > 0 && value[0]) {
        snprintf(title, size, "%s", (char *)value);
        XFree(value);
        return;
    }
    if (value)
        XFree(value);

    XTextProperty property = {0};
    if (XGetWMName(dpy, w, &property) && property.value) {
        char **list = NULL;
        int n = 0;
        if (Xutf8TextPropertyToTextList(dpy, &property, &list, &n) >= Success &&
            list && n > 0 && list[0][0])
            snprintf(title, size, "%s", list[0]);
        if (list)
            XFreeStringList(list);
        XFree(property.value);
    }
}

static void draw_overview(Display *dpy)
{
    if (!overview_visible || !overview_font)
        return;

    XClearWindow(dpy, overview);
    int margin = overview_width / 24;
    if (margin < 8) margin = 8;
    int gap = overview_width / 64;
    if (gap < 8) gap = 8;
    if (gap > 20) gap = 20;
    int width = overview_width - 2 * margin;
    if (width > 1320) width = 1320;
    int height = overview_height - 120;
    if (height > 810) height = 810;
    if (width < 90 || height < 90)
        return;
    int left = (overview_width - width) / 2;
    int top = (overview_height - height) / 2;
    int card_w = (width - 2 * gap) / 3;
    int card_h = (height - 2 * gap) / 3;

    XSetForeground(dpy, overview_gc, overview_text.pixel);
    for (int ws = 0; ws < NUM_WS; ws++) {
        int x = left + (ws % 3) * (card_w + gap);
        int y = top + (ws / 3) * (card_h + gap);
        XSetLineAttributes(dpy, overview_gc, ws == focused_ws ? 3 : 1,
                           LineSolid, CapButt, JoinMiter);
        XDrawRectangle(dpy, overview, overview_gc, x + 1, y + 1,
                       card_w - 3, card_h - 3);
        XSetLineAttributes(dpy, overview_gc, 1, LineSolid, CapButt, JoinMiter);

        char number[2] = {(char)('1' + ws), '\0'};
        overview_label(dpy, x + 16, y + 29, card_w - 32, number);
        if (workspaces[ws] == None || card_h < 85 || card_w < 64)
            continue;

        XClassHint app = {0};
        XGetClassHint(dpy, workspaces[ws], &app);
        const char *name = app.res_class && app.res_class[0] ? app.res_class :
                           app.res_name && app.res_name[0] ? app.res_name : "";
        int baseline = y + card_h / 2;
        overview_label(dpy, x + 16, baseline, card_w - 32, name);
        char title[1024];
        window_title(dpy, workspaces[ws], title, sizeof(title));
        overview_label(dpy, x + 16, baseline + 25, card_w - 32, title);
        if (app.res_name) XFree(app.res_name);
        if (app.res_class) XFree(app.res_class);
    }
}

static void update_overview(Display *dpy)
{
    if (!overview_visible)
        return;
    struct monitor_geo *mon = &monitors[workspace_monitor(focused_ws)];
    overview_width = mon->w;
    overview_height = mon->h;
    XMoveResizeWindow(dpy, overview, mon->x, mon->y, mon->w, mon->h);
    XRaiseWindow(dpy, overview);
    draw_overview(dpy);
}

static void show_overview(Display *dpy)
{
    if (overview_visible || !overview_font || !overview_draw)
        return;
    overview_visible = 1;
    update_overview(dpy);
    XMapRaised(dpy, overview);
    /* owner_events=False routes even unmodified keys to this modal window. */
    if (XGrabKeyboard(dpy, overview, False, GrabModeAsync, GrabModeAsync,
                      CurrentTime) != GrabSuccess) {
        overview_visible = 0;
        XUnmapWindow(dpy, overview);
        return;
    }
    /* Don't let clicks reach applications on another monitor while choosing. */
    if (XGrabPointer(dpy, overview, False, ButtonPressMask | ButtonReleaseMask,
                     GrabModeAsync, GrabModeAsync, None, None, CurrentTime) != GrabSuccess) {
        XUngrabKeyboard(dpy, CurrentTime);
        overview_visible = 0;
        XUnmapWindow(dpy, overview);
        return;
    }
    XSetInputFocus(dpy, overview, RevertToPointerRoot, CurrentTime);
}

static void hide_overview(Display *dpy)
{
    if (!overview_visible)
        return;
    overview_visible = 0;
    XUngrabKeyboard(dpy, CurrentTime);
    XUngrabPointer(dpy, CurrentTime);
    XUnmapWindow(dpy, overview);
    focus_window(dpy, workspaces[focused_ws]);
}

static void handle_overview_key(Display *dpy, KeySym sym)
{
    int target = -1;
    if (sym >= XK_1 && sym <= XK_9)
        target = (int)(sym - XK_1);
    else if (sym >= XK_KP_1 && sym <= XK_KP_9)
        target = (int)(sym - XK_KP_1);
    else if (sym == XK_Escape) {
        hide_overview(dpy);
        return;
    }

    if (target >= 0) {
        hide_overview(dpy);
        switch_ws(dpy, target);
    }
}

static void handle_keypress(Display *dpy, XKeyEvent *ke)
{
    KeySym sym = XLookupKeysym(ke, 0);
    const unsigned int st = ke->state;

    if (overview_visible) {
        char text[32];
        XLookupString(ke, text, sizeof(text), &sym, NULL);
        handle_overview_key(dpy, sym);
        return;
    }

    if (sym == XK_w && (st & ControlMask) && !(st & (ShiftMask | Mod1Mask | Mod4Mask))) {
        show_overview(dpy);
        return;
    }

    if (sym == XK_q && (st & ControlMask) && (st & ShiftMask)) {
        Window w = workspaces[focused_ws];
        close_window(dpy, w);
        return;
    }

    if (sym == XK_c && (st & ControlMask) && (st & ShiftMask)) {
        running = 0;
        return;
    }

    if (!(st & ControlMask)) return;
    if (sym < XK_1 || sym > XK_9) return;

    const int target = (int)(sym - XK_1);

    if (st & ShiftMask) move_window_to_ws(dpy, target);
    else               switch_ws(dpy, target);
}

int main(void)
{
    Display *dpy;
    Window root;
    XEvent ev;
    int rr_event_base = 0;
    int rr_error_base = 0;
    int have_randr = 0;

    setlocale(LC_CTYPE, "");
    XSetLocaleModifiers("");
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "cannot open display\n");
        return 1;
    }

    for (int i = 0; i < NUM_WS; i++)
        workspaces[i] = None;

    root = DefaultRootWindow(dpy);

    refresh_monitors(dpy);
    ensure_active_workspaces();
    create_ws_indicator(dpy, root);
    create_overview(dpy, root);

    XSetErrorHandler(xerror);
    XSelectInput(dpy, root,
        SubstructureRedirectMask | SubstructureNotifyMask | EnterWindowMask);

    have_randr = XRRQueryExtension(dpy, &rr_event_base, &rr_error_base);
    if (have_randr) {
        XRRSelectInput(dpy, root,
                       RRScreenChangeNotifyMask |
                       RRCrtcChangeNotifyMask |
                       RROutputChangeNotifyMask |
                       RROutputPropertyNotifyMask);
    }

    XSync(dpy, False);

    /* Keep Ctrl+W usable with Caps Lock and Num Lock enabled. */
    unsigned int numlock_mask = 0;
    XModifierKeymap *modifiers = XGetModifierMapping(dpy);
    KeyCode numlock = XKeysymToKeycode(dpy, XK_Num_Lock);
    if (modifiers) {
        for (int mod = 0; mod < 8; mod++) {
            for (int key = 0; key < modifiers->max_keypermod; key++) {
                if (numlock && modifiers->modifiermap[mod * modifiers->max_keypermod + key] == numlock)
                    numlock_mask |= 1U << mod;
            }
        }
        XFreeModifiermap(modifiers);
    }
    unsigned int locks[] = {0, LockMask, numlock_mask, LockMask | numlock_mask};
    KeyCode overview_key = XKeysymToKeycode(dpy, XK_w);
    for (unsigned int i = 0; i < sizeof(locks) / sizeof(locks[0]); i++)
        XGrabKey(dpy, overview_key, ControlMask | locks[i],
                 root, True, GrabModeAsync, GrabModeAsync);

    KeyCode q = XKeysymToKeycode(dpy, XK_Q);
    XGrabKey(dpy, q, ControlMask | ShiftMask,
             root, True, GrabModeAsync, GrabModeAsync);

    KeyCode c = XKeysymToKeycode(dpy, XK_C);
    XGrabKey(dpy, c, ControlMask | ShiftMask,
             root, True, GrabModeAsync, GrabModeAsync);

    for (int i = 0; i < NUM_WS; i++) {
        KeyCode kc = XKeysymToKeycode(dpy, XK_1 + i);

        XGrabKey(dpy, kc, ControlMask,
                 root, True, GrabModeAsync, GrabModeAsync);

        XGrabKey(dpy, kc, ControlMask | ShiftMask,
                 root, True, GrabModeAsync, GrabModeAsync);
    }

    while (running) {
        XNextEvent(dpy, &ev);

        if (have_randr &&
            (ev.type == rr_event_base + RRScreenChangeNotify ||
             ev.type == rr_event_base + RRNotify)) {
            XRRUpdateConfiguration(&ev);
            if (sync_monitors(dpy)) {
                apply_layout(dpy);
                focus_window(dpy, workspaces[focused_ws]);
            }
            continue;
        }

        switch (ev.type) {

        case Expose:
            if (ev.xexpose.window == ws_indicator)
                draw_ws_indicator(dpy);
            else if (ev.xexpose.window == overview && ev.xexpose.count == 0)
                draw_overview(dpy);
            break;

        case KeyPress:
            if (sync_monitors(dpy)) {
                apply_layout(dpy);
                focus_window(dpy, workspaces[focused_ws]);
            }
            handle_keypress(dpy, &ev.xkey);
            break;

        case MapRequest: {
            if (sync_monitors(dpy)) {
                apply_layout(dpy);
                focus_window(dpy, workspaces[focused_ws]);
            }

            XMapRequestEvent *e = &ev.xmaprequest;
            Window w = e->window;

            XSelectInput(dpy, w, EnterWindowMask | PropertyChangeMask);

            int existing = find_workspace_by_window(w);
            if (existing >= 0)
                workspaces[existing] = None;

            int mon = workspace_monitor(focused_ws);
            int target_ws = active_ws[mon];

            if (workspaces[target_ws] != None) {
                int empty = find_empty_ws_for_monitor(mon);
                if (empty >= 0)
                    target_ws = empty;
            }

            if (workspaces[target_ws] != None)
                XUnmapWindow(dpy, workspaces[target_ws]);

            workspaces[target_ws] = w;
            if (!overview_visible) {
                active_ws[mon] = target_ws;
                focused_ws = target_ws;
            }

            apply_layout(dpy);
            focus_window(dpy, w);
        } break;

        case EnterNotify: {
            XCrossingEvent *e = &ev.xcrossing;
            if (e->mode != NotifyNormal || e->detail == NotifyInferior)
                break;
            if (overview_visible || e->window == ws_indicator || e->window == overview)
                break;
            focus_monitor(dpy, monitor_at(e->x_root, e->y_root));
        } break;

        case DestroyNotify: {
            XDestroyWindowEvent *e = &ev.xdestroywindow;
            int ws = find_workspace_by_window(e->window);
            if (ws >= 0) {
                workspaces[ws] = None;
                draw_overview(dpy);
            }
        } break;

        case PropertyNotify:
            if (find_workspace_by_window(ev.xproperty.window) >= 0 &&
                (ev.xproperty.atom == net_wm_name || ev.xproperty.atom == XA_WM_NAME ||
                 ev.xproperty.atom == XA_WM_CLASS))
                draw_overview(dpy);
            break;

        case UnmapNotify: {
            XUnmapEvent *e = &ev.xunmap;
            if (e->send_event) {
                int ws = find_workspace_by_window(e->window);
                if (ws >= 0) {
                    workspaces[ws] = None;
                    draw_overview(dpy);
                }
            }
        } break;

        default:
            break;
        }
    }

    hide_overview(dpy);
    if (overview_font)
        XftFontClose(dpy, overview_font);
    if (overview_draw)
        XftDrawDestroy(overview_draw);
    if (overview_gc)
        XFreeGC(dpy, overview_gc);
    if (overview != None)
        XDestroyWindow(dpy, overview);

    for (int i = 0; i < NUM_WS; i++) {
        if (workspaces[i] != None) {
            XKillClient(dpy, workspaces[i]);
            workspaces[i] = None;
        }
    }

    XSync(dpy, False);

    if (ws_indicator_font)
        XFreeFont(dpy, ws_indicator_font);
    if (ws_indicator_gc)
        XFreeGC(dpy, ws_indicator_gc);
    if (ws_indicator != None)
        XDestroyWindow(dpy, ws_indicator);

    XCloseDisplay(dpy);
    printf("WM exited.\n");
    return 0;
}
