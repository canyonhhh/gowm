#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>
#include <locale.h>
#include <stdio.h>
#include "bindings.h"
#include "workspace.h"
#include "monitors.h"
#include "ui.h"

typedef struct {
    Display *dpy;
    Window root;
    bool running;
    WorkspaceState state;
    MonitorSet monitors;
    Ui ui;
    Atom wm_protocols, wm_delete;
    bool have_randr;
    int rr_event_base;
    unsigned int numlock_mask;
} Wm;

static int xerror(Display *dpy, XErrorEvent *error)
{
    (void)dpy;
    fprintf(stderr, "X error: request=%d error=%d\n",
            error->request_code, error->error_code);
    return 0;
}

/* Xlib's error callback cannot take a context pointer. Only used at startup. */
static bool startup_failed;

static int startup_error(Display *dpy, XErrorEvent *error)
{
    startup_failed = true;
    return xerror(dpy, error);
}

static void apply_layout(Wm *wm)
{
    for (int ws = 0; ws < NUM_WS; ws++) {
        Window window = wm->state.windows[ws];
        if (window == None)
            continue;
        int monitor = workspace_monitor(&wm->state, ws);
        if (wm->state.active[monitor] == ws) {
            MonitorGeometry geometry = wm->monitors.geometry[monitor];
            XMoveResizeWindow(wm->dpy, window, geometry.x, geometry.y,
                              (unsigned int)geometry.w, (unsigned int)geometry.h);
            XMapWindow(wm->dpy, window);
            XRaiseWindow(wm->dpy, window);
        } else {
            XUnmapWindow(wm->dpy, window);
        }
    }
}

/* Focus never maps a client. Layout alone decides which clients are visible. */
static void apply_focus(Wm *wm)
{
    Window window = wm->ui.overview.visible ? wm->ui.overview.window :
                    wm->state.windows[wm->state.focused];
    XSetInputFocus(wm->dpy, window == None ? wm->root : window,
                   RevertToPointerRoot, CurrentTime);
}

static void present(Wm *wm)
{
    apply_layout(wm);
    ui_update(&wm->ui, &wm->state, &wm->monitors);
    apply_focus(wm);
}

static bool sync_monitors(Wm *wm)
{
    MonitorSet monitors;
    monitors_query(wm->dpy, &monitors);
    if (monitors_equal(&monitors, &wm->monitors))
        return false;
    wm->monitors = monitors;
    workspace_set_monitor_count(&wm->state, monitors.count);
    return true;
}

static void center_pointer(Wm *wm, int monitor)
{
    MonitorGeometry geometry = wm->monitors.geometry[monitor];
    XWarpPointer(wm->dpy, None, wm->root, 0, 0, 0, 0,
                  geometry.x + geometry.w / 2, geometry.y + geometry.h / 2);
}

static void close_window(Wm *wm)
{
    Window window = wm->state.windows[wm->state.focused];
    if (window == None)
        return;
    Atom *protocols = NULL;
    int count = 0;
    if (XGetWMProtocols(wm->dpy, window, &protocols, &count)) {
        for (int i = 0; i < count; i++) {
            if (protocols[i] == wm->wm_delete) {
                XEvent event = {0};
                event.xclient.type = ClientMessage;
                event.xclient.window = window;
                event.xclient.message_type = wm->wm_protocols;
                event.xclient.format = 32;
                event.xclient.data.l[0] = wm->wm_delete;
                event.xclient.data.l[1] = CurrentTime;
                XSendEvent(wm->dpy, window, False, NoEventMask, &event);
                XFree(protocols);
                return;
            }
        }
        XFree(protocols);
    }
    XDestroyWindow(wm->dpy, window);
}

static void run_action(Wm *wm, Action action, int argument)
{
    int previous_monitor = workspace_monitor(&wm->state, wm->state.focused);
    switch (action) {
    case ACTION_OVERVIEW:
        ui_show_overview(&wm->ui, &wm->state, &wm->monitors);
        break;
    case ACTION_CLOSE:
        close_window(wm);
        break;
    case ACTION_QUIT:
        wm->running = false;
        break;
    case ACTION_SWITCH:
        if (workspace_select(&wm->state, argument)) {
            present(wm);
            int monitor = workspace_monitor(&wm->state, argument);
            if (monitor != previous_monitor)
                center_pointer(wm, monitor);
        }
        break;
    case ACTION_MOVE:
        if (workspace_move(&wm->state, argument))
            present(wm);
        break;
    }
}

static void handle_keypress(Wm *wm, XKeyEvent *event)
{
    KeySym key = XLookupKeysym(event, 0);
    if (wm->ui.overview.visible) {
        char text[32];
        XLookupString(event, text, sizeof(text), &key, NULL);
        int target = -1;
        if (key >= XK_1 && key <= XK_9)
            target = (int)(key - XK_1);
        else if (key >= XK_KP_1 && key <= XK_KP_9)
            target = (int)(key - XK_KP_1);
        if (target >= 0 || key == XK_Escape) {
            ui_hide_overview(&wm->ui);
            if (target >= 0)
                run_action(wm, ACTION_SWITCH, target);
            else
                present(wm);
        }
        return;
    }
    /* Match keyboard modifiers only, ignoring lock and mouse-button bits. */
    unsigned int modifiers = event->state & (ShiftMask | ControlMask | Mod1Mask |
        Mod2Mask | Mod3Mask | Mod4Mask | Mod5Mask) & ~wm->numlock_mask;
    for (size_t i = 0; i < sizeof(bindings) / sizeof(bindings[0]); i++) {
        if (bindings[i].key == key && bindings[i].modifiers == modifiers) {
            run_action(wm, bindings[i].action, bindings[i].argument);
            return;
        }
    }
}

static void grab_keys(Wm *wm)
{
    wm->numlock_mask = 0;
    XModifierKeymap *map = XGetModifierMapping(wm->dpy);
    KeyCode numlock = XKeysymToKeycode(wm->dpy, XK_Num_Lock);
    if (map) {
        for (int mod = 0; mod < 8; mod++) {
            for (int key = 0; key < map->max_keypermod; key++) {
                if (numlock && map->modifiermap[mod * map->max_keypermod + key] == numlock)
                    wm->numlock_mask |= 1U << mod;
            }
        }
        XFreeModifiermap(map);
    }
    XUngrabKey(wm->dpy, AnyKey, AnyModifier, wm->root);
    unsigned int locks[] = {0, LockMask, wm->numlock_mask, LockMask | wm->numlock_mask};
    for (size_t i = 0; i < sizeof(bindings) / sizeof(bindings[0]); i++) {
        KeyCode key = XKeysymToKeycode(wm->dpy, bindings[i].key);
        if (!key)
            continue;
        for (size_t lock = 0; lock < sizeof(locks) / sizeof(locks[0]); lock++)
            XGrabKey(wm->dpy, key, bindings[i].modifiers | locks[lock],
                      wm->root, True, GrabModeAsync, GrabModeAsync);
    }
}

static void handle_map_request(Wm *wm, Window window)
{
    XSelectInput(wm->dpy, window, EnterWindowMask | PropertyChangeMask);
    WorkspaceAdmission result = workspace_admit(&wm->state, window, !wm->ui.overview.visible);
    if (result == WORKSPACE_NO_MEMORY) {
        /* Do not hide an application we cannot retain in the queue. */
        fprintf(stderr, "cannot track window %lu: out of memory; leaving unmanaged\n", window);
        XMapWindow(wm->dpy, window);
    }
    present(wm);
}

static void handle_enter(Wm *wm, const XCrossingEvent *event)
{
    if (event->mode != NotifyNormal || event->detail == NotifyInferior ||
        wm->ui.overview.visible || ui_owns_window(&wm->ui, event->window))
        return;
    int monitor = monitors_at(&wm->monitors, event->x_root, event->y_root);
    if (monitor == workspace_monitor(&wm->state, wm->state.focused))
        return;
    if (workspace_focus_monitor(&wm->state, monitor)) {
        ui_update(&wm->ui, &wm->state, &wm->monitors);
        apply_focus(wm);
    }
}

static void remove_window(Wm *wm, Window window)
{
    if (workspace_remove(&wm->state, window))
        present(wm);
}

static void dispatch_event(Wm *wm, XEvent *event)
{
    if (wm->have_randr &&
        (event->type == wm->rr_event_base + RRScreenChangeNotify ||
         event->type == wm->rr_event_base + RRNotify)) {
        XRRUpdateConfiguration(event);
        if (sync_monitors(wm))
            present(wm);
        return;
    }
    /* Keep the fallback query for servers whose topology notifications are
     * incomplete. Discovery never directly changes selections or focus. */
    if ((event->type == KeyPress || event->type == MapRequest) && sync_monitors(wm))
        present(wm);
    switch (event->type) {
    case Expose:
        ui_expose(&wm->ui, &event->xexpose, &wm->state);
        break;
    case KeyPress:
        handle_keypress(wm, &event->xkey);
        break;
    case MappingNotify:
        XRefreshKeyboardMapping(&event->xmapping);
        if (event->xmapping.request != MappingPointer)
            grab_keys(wm);
        break;
    case MapRequest:
        handle_map_request(wm, event->xmaprequest.window);
        break;
    case EnterNotify:
        handle_enter(wm, &event->xcrossing);
        break;
    case DestroyNotify:
        remove_window(wm, event->xdestroywindow.window);
        break;
    case UnmapNotify:
        /* Synthetic withdrawal notifications distinguish client withdrawal
         * from the ordinary unmaps issued by apply_layout(). */
        if (event->xunmap.send_event)
            remove_window(wm, event->xunmap.window);
        break;
    case PropertyNotify:
        if (workspace_contains(&wm->state, event->xproperty.window) &&
            ui_title_property(&wm->ui, event->xproperty.atom))
            ui_draw_overview(&wm->ui, &wm->state);
        break;
    default:
        break;
    }
}

static bool init_wm(Wm *wm)
{
    wm->dpy = XOpenDisplay(NULL);
    if (!wm->dpy) {
        fprintf(stderr, "cannot open display\n");
        return false;
    }
    wm->root = DefaultRootWindow(wm->dpy);
    XSetErrorHandler(startup_error);
    XSelectInput(wm->dpy, wm->root,
                 SubstructureRedirectMask | SubstructureNotifyMask | EnterWindowMask);
    XSync(wm->dpy, False);
    XSetErrorHandler(xerror);
    if (startup_failed) {
        fprintf(stderr, "cannot manage root window; another window manager may be running\n");
        XCloseDisplay(wm->dpy);
        return false;
    }
    wm->wm_protocols = XInternAtom(wm->dpy, "WM_PROTOCOLS", False);
    wm->wm_delete = XInternAtom(wm->dpy, "WM_DELETE_WINDOW", False);
    int rr_error_base;
    wm->have_randr = XRRQueryExtension(wm->dpy, &wm->rr_event_base, &rr_error_base);
    if (wm->have_randr)
        XRRSelectInput(wm->dpy, wm->root, RRScreenChangeNotifyMask |
                       RRCrtcChangeNotifyMask | RROutputChangeNotifyMask |
                       RROutputPropertyNotifyMask);
    monitors_query(wm->dpy, &wm->monitors);
    workspace_init(&wm->state, wm->monitors.count);
    ui_init(&wm->ui, wm->dpy, wm->root);
    grab_keys(wm);
    present(wm);
    XSync(wm->dpy, False);
    wm->running = true;
    return true;
}

static void destroy_wm(Wm *wm)
{
    ui_destroy(&wm->ui);
    /* Preserve gowm's existing exit policy, including clients waiting for a
     * workspace. Never drop a queued client's XID before cleanup. */
    for (int ws = 0; ws < NUM_WS; ws++) {
        if (wm->state.windows[ws] != None)
            XKillClient(wm->dpy, wm->state.windows[ws]);
    }
    for (PendingWindow *pending = wm->state.pending_head; pending; pending = pending->next)
        XKillClient(wm->dpy, pending->window);
    workspace_destroy(&wm->state);
    XSync(wm->dpy, False);
    XCloseDisplay(wm->dpy);
}

int main(void)
{
    setlocale(LC_CTYPE, "");
    XSetLocaleModifiers("");
    Wm wm = {0};
    if (!init_wm(&wm))
        return 1;
    while (wm.running) {
        XEvent event;
        XNextEvent(wm.dpy, &event);
        dispatch_event(&wm, &event);
    }
    destroy_wm(&wm);
    puts("WM exited.");
    return 0;
}
