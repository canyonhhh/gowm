#ifndef GOWM_WORKSPACE_H
#define GOWM_WORKSPACE_H

#include <X11/X.h>
#include <stdbool.h>
#include "config.h"

typedef struct PendingWindow {
    Window window;
    int preferred_monitor;
    struct PendingWindow *next;
} PendingWindow;

typedef struct {
    Window windows[NUM_WS];
    int active[NUM_MONITORS];
    int focused;
    int monitor_count;
    PendingWindow *pending_head;
    PendingWindow *pending_tail;
} WorkspaceState;

typedef enum {
    WORKSPACE_ADMITTED,
    WORKSPACE_KNOWN,
    WORKSPACE_QUEUED,
    WORKSPACE_NO_MEMORY
} WorkspaceAdmission;

/* State transitions never call Xlib. The focused workspace is always active
 * on its assigned monitor. A window is in one slot or the pending queue. */
void workspace_init(WorkspaceState *state, int monitor_count);
void workspace_destroy(WorkspaceState *state);
int workspace_monitor(const WorkspaceState *state, int ws);
int workspace_find(const WorkspaceState *state, Window window);
bool workspace_contains(const WorkspaceState *state, Window window);
bool workspace_select(WorkspaceState *state, int target);
bool workspace_focus_monitor(WorkspaceState *state, int monitor);
bool workspace_move(WorkspaceState *state, int target);
void workspace_set_monitor_count(WorkspaceState *state, int monitor_count);
/* window must be a non-None client XID. Known clients keep their assignment.
 * activate=false places new clients without changing focus, for the overview.
 * Full monitors queue clients; allocation failure leaves state unchanged. */
WorkspaceAdmission workspace_admit(WorkspaceState *state, Window window, bool activate);
/* Remove either a slotted or queued client, filling newly available slots. */
bool workspace_remove(WorkspaceState *state, Window window);

#endif
