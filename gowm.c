#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>

#define NUM_WS 9

static int running = 1;
static Window workspaces[NUM_WS];
static int current_ws = 0;

static int xerror(Display *dpy, XErrorEvent *ee)
{
    (void)dpy;
    fprintf(stderr, "X error: request=%d error=%d\n",
            ee->request_code, ee->error_code);
    return 0;
}

static int find_empty_ws(void)
{
    for (int i = 0; i < NUM_WS; i++) {
        if (workspaces[i] == None)
            return i;
    }
    return -1;
}

static void focus_window(Display *dpy, Window w)
{
    if (w == None) return;
    XMapRaised(dpy, w);
    XSetInputFocus(dpy, w, RevertToPointerRoot, CurrentTime);
}

static void switch_ws(Display *dpy, int target)
{
    if (target == current_ws) return;

    if (workspaces[current_ws] != None)
        XUnmapWindow(dpy, workspaces[current_ws]);

    current_ws = target;
    focus_window(dpy, workspaces[current_ws]);
}

static void move_window_to_ws(Display *dpy, int target)
{
    if (target == current_ws) return;

    Window w = workspaces[current_ws];
    if (w == None) return;

    if (workspaces[target] != None) return;

    workspaces[current_ws] = None;
    workspaces[target] = w;

    focus_window(dpy, w);
}

static void handle_keypress(Display *dpy, XKeyEvent *ke)
{
    const KeySym sym = XLookupKeysym(ke, 0);
    const unsigned int st = ke->state;

    if (sym == XK_q && (st & ControlMask) && (st & ShiftMask)) {
        Window w = workspaces[current_ws];
        if (w != None) {
            XKillClient(dpy, w);
            workspaces[current_ws] = None;
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

    int screen;
    unsigned int sw, sh;

    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "cannot open display\n");
        return 1;
    }

    for (int i = 0; i < NUM_WS; i++)
        workspaces[i] = None;

    root = DefaultRootWindow(dpy);

    screen = DefaultScreen(dpy);
    sw = DisplayWidth(dpy, screen);
    sh = DisplayHeight(dpy, screen);

    XSetErrorHandler(xerror);
    XSelectInput(dpy, root,
        SubstructureRedirectMask | SubstructureNotifyMask);

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

        switch (ev.type) {

        case KeyPress:
            handle_keypress(dpy, &ev.xkey);
            break;

        case MapRequest: {
            XMapRequestEvent *e = &ev.xmaprequest;
            Window w = e->window;

            int target_ws = current_ws;

            if (workspaces[target_ws] != None) {
                int empty = find_empty_ws();
                if (empty >= 0)
                    target_ws = empty;
            }

            if (workspaces[target_ws] != None) {
                XUnmapWindow(dpy, workspaces[target_ws]);
            }

            workspaces[target_ws] = w;

            XMoveResizeWindow(dpy, w, 0, 0, sw, sh);
            XMapWindow(dpy, w);
            XRaiseWindow(dpy, w);

            if (target_ws != current_ws) {
                current_ws = target_ws;
            }

            XSetInputFocus(dpy, w, RevertToPointerRoot, CurrentTime);
        } break;

        case DestroyNotify: {
            XDestroyWindowEvent *e = &ev.xdestroywindow;
            if (e->window == workspaces[current_ws]) {
                workspaces[current_ws] = None;
            }
        } break;

        case UnmapNotify: {
            XUnmapEvent *e = &ev.xunmap;
            if (e->window == workspaces[current_ws]) {
                workspaces[current_ws] = None;
            }
        } break;

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

    XCloseDisplay(dpy);
    printf("WM exited.\n");
    return 0;
}
