#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/XKBlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static Display *dpy;
static Window root;
static const char *scenario = "startup";

static void fail(const char *condition, int line)
{
    fprintf(stderr, "%s:%d: timed out or failed: %s\n", scenario, line, condition);
    exit(1);
}

static void tick(void)
{
    struct timespec delay = {0, 10000000};
    nanosleep(&delay, NULL);
}

/* XSync plus a bounded condition poll, never a fixed startup delay. */
#define WAIT(condition) do { \
    int tries; \
    for (tries = 0; tries < 300; tries++) { \
        XSync(dpy, False); \
        if (condition) break; \
        tick(); \
    } \
    if (tries == 300) fail(#condition, __LINE__); \
} while (0)
#define CHECK(condition) do { if (!(condition)) fail(#condition, __LINE__); } while (0)

static int ready(void)
{
    XWindowAttributes attr;
    XGetWindowAttributes(dpy, root, &attr);
    /* Every scenario then waits for its first client to receive focus. That
       MapRequest is handled only after startup has installed the key grabs.
       Probing with XGrabKey here could steal a grab during WM startup. */
    return (attr.all_event_masks & SubstructureRedirectMask) != 0;
}

static int mapped(Window window)
{
    XWindowAttributes attr;
    CHECK(XGetWindowAttributes(dpy, window, &attr));
    return attr.map_state == IsViewable;
}

static Window focused(void)
{
    Window window;
    int revert;
    XGetInputFocus(dpy, &window, &revert);
    return window;
}

static void key(KeySym symbol, unsigned int modifiers)
{
    KeyCode code = XKeysymToKeycode(dpy, symbol);
    CHECK(code);
    if (modifiers & ControlMask)
        XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Control_L), True, 0);
    if (modifiers & ShiftMask)
        XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Shift_L), True, 0);
    XTestFakeKeyEvent(dpy, code, True, 0);
    XTestFakeKeyEvent(dpy, code, False, 0);
    if (modifiers & ShiftMask)
        XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Shift_L), False, 0);
    if (modifiers & ControlMask)
        XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Control_L), False, 0);
    XSync(dpy, False);
}

static Window client(void)
{
    Window window = XCreateSimpleWindow(dpy, root, 30, 30, 160, 100, 0, 0, 0);
    XStoreName(dpy, window, "integration client");
    Atom protocol = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, window, &protocol, 1);
    XMapWindow(dpy, window);
    XFlush(dpy);
    return window;
}

static Window visible_client(void)
{
    Window window = client();
    WAIT(mapped(window) && focused() == window);
    return window;
}

static Window overview;

static int overview_focused(void)
{
    Window window = focused();
    if (window == None || window == PointerRoot || window == root) return 0;
    char *name = NULL;
    int found = XFetchName(dpy, window, &name) && name &&
        !strcmp(name, "gowm workspace overview");
    if (name) XFree(name);
    if (found) overview = window;
    return found && mapped(window);
}

static void open_overview(void)
{
    key(XK_w, ControlMask);
    WAIT(overview_focused());
}

static int grabs(int held)
{
    int keyboard = XGrabKeyboard(dpy, root, False, GrabModeAsync,
                                 GrabModeAsync, CurrentTime);
    int pointer = XGrabPointer(dpy, root, False, ButtonPressMask,
                               GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
    if (keyboard == GrabSuccess) XUngrabKeyboard(dpy, CurrentTime);
    if (pointer == GrabSuccess) XUngrabPointer(dpy, CurrentTime);
    XSync(dpy, False);
    int expected = held ? AlreadyGrabbed : GrabSuccess;
    return keyboard == expected && pointer == expected;
}

static void pick(KeySym symbol, Window expected)
{
    key(symbol, 0);
    WAIT(!mapped(overview) && focused() == expected);
    WAIT(grabs(0));
}

static void test_switch(void)
{
    Window first = visible_client(), second = visible_client();
    for (int i = 0; i < 3; i++) {
        key(XK_1, ControlMask);
        WAIT(mapped(first) && !mapped(second) && focused() == first);
        key(XK_2, ControlMask);
        WAIT(mapped(second) && !mapped(first) && focused() == second);
    }
    key(XK_9, ControlMask);
    WAIT(!mapped(first) && !mapped(second) && focused() == root);
}

static void test_swap(void)
{
    Window first = visible_client(), second = visible_client();
    key(XK_1, ControlMask | ShiftMask);
    WAIT(mapped(second) && !mapped(first) && focused() == second);
    key(XK_2, ControlMask);
    WAIT(mapped(first) && !mapped(second) && focused() == first);
    key(XK_1, ControlMask);
    WAIT(mapped(second) && !mapped(first) && focused() == second);
}

static void test_overview(void)
{
    Window first = visible_client(), second = visible_client();
    open_overview();
    WAIT(grabs(1));
    pick(XK_Escape, second);
    CHECK(mapped(second) && !mapped(first));
    open_overview();
    pick(XK_1, first);
    CHECK(mapped(first) && !mapped(second));
    open_overview();
    pick(XK_9, root);
    CHECK(!mapped(first) && !mapped(second));
    key(XK_Num_Lock, 0);
    open_overview();
    pick(XK_KP_2, second);
    CHECK(mapped(second) && !mapped(first));
    key(XK_Num_Lock, 0);
}

static void test_lifecycle(void)
{
    Window first = visible_client(), second = visible_client();
    open_overview();
    Window third = client();
    XStoreName(dpy, second, "changed WM_NAME");
    Atom utf8 = XInternAtom(dpy, "UTF8_STRING", False);
    Atom title = XInternAtom(dpy, "_NET_WM_NAME", False);
    const unsigned char text[] = "changed UTF-8 title: \xc3\xa9";
    XChangeProperty(dpy, second, title, utf8, 8, PropModeReplace, text, sizeof(text) - 1);
    XClassHint app = {"integration", "ChangedClass"};
    XSetClassHint(dpy, second, &app);
    XDestroyWindow(dpy, first);
    /* Escape is sent after the create/property/destroy requests. Reopening
       also proves the WM stayed alive and released the previous modal grab. */
    pick(XK_Escape, second);
    open_overview();
    pick(XK_3, third);
    CHECK(mapped(third) && !mapped(second));
    open_overview();
    XDestroyWindow(dpy, third);
    pick(XK_2, second);
    open_overview();
    pick(XK_3, root);
}

static void test_overview_locks(void)
{
    Window window = visible_client();
    const KeySym toggles[] = {XK_Caps_Lock, XK_Num_Lock, XK_Caps_Lock};
    for (size_t i = 0; i < sizeof(toggles) / sizeof(toggles[0]); i++) {
        key(toggles[i], 0);
        open_overview();
        pick(XK_Escape, window);
    }
}

static int delete_requested(Window window)
{
    Atom protocol = XInternAtom(dpy, "WM_PROTOCOLS", False);
    Atom deletion = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    while (XPending(dpy)) {
        XEvent event;
        XNextEvent(dpy, &event);
        if (event.type == ClientMessage && event.xclient.window == window &&
            event.xclient.message_type == protocol && event.xclient.format == 32 &&
            (Atom)event.xclient.data.l[0] == deletion) return 1;
    }
    return 0;
}

static void test_shortcut_locks(void)
{
    Window first = visible_client(), second = visible_client();
    if (strcmp(scenario, "num-shortcuts")) key(XK_Caps_Lock, 0);
    if (strcmp(scenario, "caps-shortcuts")) key(XK_Num_Lock, 0);
    key(XK_1, ControlMask);
    WAIT(mapped(first) && !mapped(second) && focused() == first);
    key(XK_2, ControlMask | ShiftMask);
    WAIT(mapped(first) && !mapped(second) && focused() == first);
    key(XK_1, ControlMask);
    WAIT(mapped(second) && !mapped(first) && focused() == second);
    key(XK_q, ControlMask | ShiftMask);
    WAIT(delete_requested(second));
    XDestroyWindow(dpy, second);
    key(XK_2, ControlMask);
    WAIT(mapped(first) && focused() == first);
}

static void test_button_shortcuts(void)
{
    Window first = visible_client(), second = visible_client();
    XTestFakeButtonEvent(dpy, 1, True, 0);
    XSync(dpy, False);
    Window pointer_root, pointer_child;
    int root_x, root_y, child_x, child_y;
    unsigned int buttons;
    CHECK(XQueryPointer(dpy, root, &pointer_root, &pointer_child,
                        &root_x, &root_y, &child_x, &child_y, &buttons));
    CHECK(buttons & Button1Mask);
    key(XK_1, ControlMask);
    WAIT(mapped(first) && !mapped(second) && focused() == first);
    open_overview();
    pick(XK_Escape, first);
    XTestFakeButtonEvent(dpy, 1, False, 0);
    XSync(dpy, False);
}

static void test_queue(void)
{
    Window windows[9];
    for (int i = 0; i < 9; i++) windows[i] = visible_client();
    Window queued = client();
    /* Opening the overview acknowledges all earlier MapRequests, including
       a request whose correct result is to leave the new client unmapped. */
    open_overview();
    CHECK(!mapped(queued) && mapped(windows[8]));
    pick(XK_Escape, windows[8]);
    XDestroyWindow(dpy, windows[8]);
    WAIT(mapped(queued) && focused() == queued);
    for (int i = 0; i < 8; i++) {
        key(XK_1 + i, ControlMask);
        WAIT(mapped(windows[i]) && !mapped(queued) && focused() == windows[i]);
    }
    key(XK_9, ControlMask);
    WAIT(mapped(queued) && focused() == queued);
}

static void test_remap(void)
{
    Window first = visible_client(), second = visible_client();
    XMapWindow(dpy, first);
    open_overview();
    CHECK(!mapped(first) && mapped(second));
    pick(XK_Escape, second);
    key(XK_1, ControlMask);
    WAIT(mapped(first) && !mapped(second) && focused() == first);
    key(XK_2, ControlMask);
    WAIT(mapped(second) && !mapped(first) && focused() == second);
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    scenario = argv[1];
    CHECK(getenv("DISPLAY") && getenv("DISPLAY")[0] == ':');
    dpy = XOpenDisplay(NULL);
    CHECK(dpy);
    root = DefaultRootWindow(dpy);
    int event, error, major, minor;
    CHECK(XTestQueryExtension(dpy, &event, &error, &major, &minor));
    WAIT(ready());
    CHECK(XkbLockModifiers(dpy, XkbUseCoreKbd, 0xff, 0));
    XSync(dpy, False);
    if (!strcmp(scenario, "switch")) test_switch();
    else if (!strcmp(scenario, "swap")) test_swap();
    else if (!strcmp(scenario, "overview")) test_overview();
    else if (!strcmp(scenario, "lifecycle")) test_lifecycle();
    else if (!strcmp(scenario, "overview-locks")) test_overview_locks();
    else if (!strcmp(scenario, "button-shortcuts")) test_button_shortcuts();
    else if (!strcmp(scenario, "queue")) test_queue();
    else if (!strcmp(scenario, "remap")) test_remap();
    else if (!strcmp(scenario, "caps-shortcuts") || !strcmp(scenario, "num-shortcuts") ||
             !strcmp(scenario, "both-shortcuts")) test_shortcut_locks();
    else fail("unknown scenario", __LINE__);
    XCloseDisplay(dpy);
    printf("PASS: %s\n", scenario);
    return 0;
}
