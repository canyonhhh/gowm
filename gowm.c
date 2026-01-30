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

static void
spawn(const char *cmd)
{
    if (fork() == 0) {
        setsid();
        execlp("sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
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

    KeyCode p = XKeysymToKeycode(dpy, XK_p);
    XGrabKey(dpy, p, ControlMask, root, True, GrabModeAsync, GrabModeAsync);

    for (int i = 0; i < NUM_WS; i++) {
        KeyCode kc = XKeysymToKeycode(dpy, XK_1 + i);

        XGrabKey(dpy, kc, ControlMask,
                 root, True, GrabModeAsync, GrabModeAsync);

        XGrabKey(dpy, kc, ControlMask | ShiftMask,
                 root, True, GrabModeAsync, GrabModeAsync);
    }

    while (running) {
        XNextEvent(dpy, &ev);

        if (ev.type == KeyPress) {

            XKeyEvent *ke = &ev.xkey;
            KeySym sym = XLookupKeysym(ke, 0);

            if ((ke->state & ControlMask) &&
                sym >= XK_1 && sym <= XK_9) {

                int target = sym - XK_1;

                if (ke->state & ShiftMask) {
                    Window w = workspaces[current_ws];

                    if (w == None || target == current_ws)
                        continue;

                    workspaces[current_ws] = None;

                    if (workspaces[target] != None) {
                        continue;
                    }

                    workspaces[target] = w;

                    XMapRaised(dpy, w);
                    XSetInputFocus(dpy, w,
                                   RevertToPointerRoot, CurrentTime);
                }
                else {
                    if (target == current_ws)
                        continue;

                    if (workspaces[current_ws] != None) {
                        XUnmapWindow(dpy, workspaces[current_ws]);
                    }

                    current_ws = target;

                    if (workspaces[current_ws] != None) {
                        Window w = workspaces[current_ws];
                        XMapRaised(dpy, w);
                        XSetInputFocus(dpy, w,
                                       RevertToPointerRoot, CurrentTime);
                    }
                }
            } else if (sym == XK_q &&
                (ke->state & ControlMask) &&
                (ke->state & ShiftMask)) {
                running = 0;
            } else if ((ke->state & ControlMask) && sym == XK_p) {
                spawn("dmenu_run");
                continue;
            }
        } else if (ev.type == MapRequest) {
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
        } else if (ev.type == DestroyNotify) {
            XDestroyWindowEvent *e = &ev.xdestroywindow;

            if (e->window == workspaces[current_ws]) {
                workspaces[current_ws] = None;
            }
        } else if (ev.type == UnmapNotify) {
            XUnmapEvent *e = &ev.xunmap;

            if (e->window == workspaces[current_ws]) {
                workspaces[current_ws] = None;
            }
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
