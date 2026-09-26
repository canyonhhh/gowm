#ifndef GOWM_MONITORS_H
#define GOWM_MONITORS_H

#include <X11/Xlib.h>
#include <stdbool.h>
#include "config.h"

typedef struct {
    int x, y, w, h;
} MonitorGeometry;

typedef struct {
    int count;
    MonitorGeometry geometry[NUM_MONITORS];
} MonitorSet;

/* Discover geometry only; workspace assignment is workspace.c's policy. */
void monitors_query(Display *dpy, MonitorSet *monitors);
bool monitors_equal(const MonitorSet *a, const MonitorSet *b);
int monitors_at(const MonitorSet *monitors, int x, int y);

#endif
