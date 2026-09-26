#include "monitors.h"
#include <X11/extensions/Xinerama.h>
#include <X11/extensions/Xrandr.h>
#include <string.h>

static int overlap_area(MonitorGeometry a, MonitorGeometry b)
{
    int left = a.x > b.x ? a.x : b.x;
    int top = a.y > b.y ? a.y : b.y;
    int right = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
    int bottom = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
    return right > left && bottom > top ? (right - left) * (bottom - top) : 0;
}

static bool internal_output(const char *name)
{
    return strncmp(name, "eDP", 3) == 0 ||
           strncmp(name, "LVDS", 4) == 0 ||
           strncmp(name, "DSI", 3) == 0;
}

/* Prefer the configured output, then the primary, then an internal panel. */
static bool main_output_geometry(Display *dpy, MonitorGeometry *geometry)
{
    int event_base, error_base;
    if (!XRRQueryExtension(dpy, &event_base, &error_base))
        return false;
    Window root = DefaultRootWindow(dpy);
    XRRScreenResources *resources = XRRGetScreenResourcesCurrent(dpy, root);
    if (!resources)
        return false;
    RROutput primary = XRRGetOutputPrimary(dpy, root);
    int best_rank = 0;
    for (int i = 0; i < resources->noutput; i++) {
        RROutput id = resources->outputs[i];
        XRROutputInfo *output = XRRGetOutputInfo(dpy, resources, id);
        if (!output)
            continue;
        if (output->connection == RR_Connected && output->crtc != None) {
            int rank = strcmp(output->name, MAIN_OUTPUT_NAME) == 0 ? 3 :
                       id == primary ? 2 : internal_output(output->name) ? 1 : 0;
            if (rank > best_rank) {
                XRRCrtcInfo *crtc = XRRGetCrtcInfo(dpy, resources, output->crtc);
                if (crtc) {
                    *geometry = (MonitorGeometry){crtc->x, crtc->y,
                                                  (int)crtc->width, (int)crtc->height};
                    best_rank = rank;
                    XRRFreeCrtcInfo(crtc);
                }
            }
        }
        XRRFreeOutputInfo(output);
    }
    XRRFreeScreenResources(resources);
    return best_rank > 0;
}

void monitors_query(Display *dpy, MonitorSet *monitors)
{
    XWindowAttributes root;
    XGetWindowAttributes(dpy, DefaultRootWindow(dpy), &root);
    *monitors = (MonitorSet){ .count = 1, .geometry = {{0, 0, root.width, root.height}} };
    if (!XineramaIsActive(dpy))
        return;
    int count = 0;
    XineramaScreenInfo *screens = XineramaQueryScreens(dpy, &count);
    if (!screens || count < 1) {
        if (screens)
            XFree(screens);
        return;
    }
    int main_index = 0;
    MonitorGeometry main_geometry;
    if (count > 1 && main_output_geometry(dpy, &main_geometry)) {
        int best_overlap = -1;
        for (int i = 0; i < count; i++) {
            MonitorGeometry candidate = {screens[i].x_org, screens[i].y_org,
                                         screens[i].width, screens[i].height};
            int overlap = overlap_area(candidate, main_geometry);
            if (overlap > best_overlap) {
                main_index = i;
                best_overlap = overlap;
            }
        }
    }
    monitors->geometry[0] = (MonitorGeometry){screens[main_index].x_org,
        screens[main_index].y_org, screens[main_index].width, screens[main_index].height};
    for (int i = 0; i < count; i++) {
        if (i != main_index) {
            monitors->count = 2;
            monitors->geometry[1] = (MonitorGeometry){screens[i].x_org,
                screens[i].y_org, screens[i].width, screens[i].height};
            break;
        }
    }
    XFree(screens);
}

bool monitors_equal(const MonitorSet *a, const MonitorSet *b)
{
    if (a->count != b->count)
        return false;
    for (int i = 0; i < a->count; i++) {
        MonitorGeometry x = a->geometry[i], y = b->geometry[i];
        if (x.x != y.x || x.y != y.y || x.w != y.w || x.h != y.h)
            return false;
    }
    return true;
}

int monitors_at(const MonitorSet *monitors, int x, int y)
{
    for (int i = 0; i < monitors->count; i++) {
        MonitorGeometry geometry = monitors->geometry[i];
        if (x >= geometry.x && x < geometry.x + geometry.w &&
            y >= geometry.y && y < geometry.y + geometry.h)
            return i;
    }
    return -1;
}
