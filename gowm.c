#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/Xinerama.h>
#include <X11/extensions/Xrandr.h>
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
    return ws == 0 || ws == 7 || ws == 8;
}

static int workspace_monitor(int ws)
{
    if (monitor_count >= 2 && is_external_workspace(ws))
        return 1;
    return 0;
}

static void focus_window(Display *dpy, Window w)
{
    if (w == None) return;
    XMapRaised(dpy, w);
    XSetInputFocus(dpy, w, RevertToPointerRoot, CurrentTime);
    if (ws_indicator != None)
        XRaiseWindow(dpy, ws_indicator);
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
            active_ws[0] = 1;
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
}

static void center_pointer_on_monitor(Display *dpy, int mon)
{
    Window root = DefaultRootWindow(dpy);
    int cx = monitors[mon].x + monitors[mon].w / 2;
    int cy = monitors[mon].y + monitors[mon].h / 2;
    XWarpPointer(dpy, None, root, 0, 0, 0, 0, cx, cy);
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

static void handle_keypress(Display *dpy, XKeyEvent *ke)
{
    const KeySym sym = XLookupKeysym(ke, 0);
    const unsigned int st = ke->state;

    if (sym == XK_q && (st & ControlMask) && (st & ShiftMask)) {
        Window w = workspaces[focused_ws];
        if (w != None) {
            XKillClient(dpy, w);
            workspaces[focused_ws] = None;
            apply_layout(dpy);
        }
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

    XSetErrorHandler(xerror);
    XSelectInput(dpy, root,
        SubstructureRedirectMask | SubstructureNotifyMask);

    have_randr = XRRQueryExtension(dpy, &rr_event_base, &rr_error_base);
    if (have_randr) {
        XRRSelectInput(dpy, root,
                       RRScreenChangeNotifyMask |
                       RRCrtcChangeNotifyMask |
                       RROutputChangeNotifyMask |
                       RROutputPropertyNotifyMask);
    }

    XSync(dpy, False);

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
            active_ws[mon] = target_ws;
            focused_ws = target_ws;

            apply_layout(dpy);
            focus_window(dpy, w);
        } break;

        case DestroyNotify: {
            XDestroyWindowEvent *e = &ev.xdestroywindow;
            int ws = find_workspace_by_window(e->window);
            if (ws >= 0)
                workspaces[ws] = None;
        } break;

        case UnmapNotify:
            break;

        default:
            break;
        }
    }

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
