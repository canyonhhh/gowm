#include "workspace.h"
#include <stdlib.h>

static bool valid_workspace(int ws)
{
    return ws >= 0 && ws < NUM_WS;
}

int workspace_monitor(const WorkspaceState *state, int ws)
{
    return state->monitor_count >= 2 && ws >= 0 && ws < EXTERNAL_WS_COUNT ? 1 : 0;
}

static int empty_workspace(const WorkspaceState *state, int monitor)
{
    int active = state->active[monitor];
    if (state->windows[active] == None)
        return active;
    for (int ws = 0; ws < NUM_WS; ws++) {
        if (workspace_monitor(state, ws) == monitor && state->windows[ws] == None)
            return ws;
    }
    return -1;
}

/* Oldest eligible client wins. A full monitor must not block the other
 * monitor's queue; disconnected external clients may use the main screen. */
static void fill_pending(WorkspaceState *state)
{
    PendingWindow **link = &state->pending_head;
    PendingWindow *previous = NULL;
    while (*link) {
        PendingWindow *pending = *link;
        int monitor = state->monitor_count == 1 ? 0 : pending->preferred_monitor;
        int target = empty_workspace(state, monitor);
        if (target < 0) {
            previous = pending;
            link = &pending->next;
            continue;
        }
        state->windows[target] = pending->window;
        *link = pending->next;
        if (state->pending_tail == pending)
            state->pending_tail = previous;
        free(pending);
    }
}

bool workspace_remove(WorkspaceState *state, Window window)
{
    int ws = workspace_find(state, window);
    if (ws >= 0) {
        state->windows[ws] = None;
        fill_pending(state);
        return true;
    }
    PendingWindow **link = &state->pending_head;
    PendingWindow *previous = NULL;
    while (*link) {
        PendingWindow *pending = *link;
        if (pending->window == window) {
            *link = pending->next;
            if (state->pending_tail == pending)
                state->pending_tail = previous;
            free(pending);
            return true;
        }
        previous = pending;
        link = &pending->next;
    }
    return false;
}

void workspace_init(WorkspaceState *state, int monitor_count)
{
    *state = (WorkspaceState){0};
    workspace_set_monitor_count(state, monitor_count);
}

void workspace_destroy(WorkspaceState *state)
{
    PendingWindow *pending = state->pending_head;
    while (pending) {
        PendingWindow *next = pending->next;
        free(pending);
        pending = next;
    }
    *state = (WorkspaceState){0};
}

int workspace_find(const WorkspaceState *state, Window window)
{
    if (window != None) {
        for (int ws = 0; ws < NUM_WS; ws++) {
            if (state->windows[ws] == window)
                return ws;
        }
    }
    return -1;
}

bool workspace_contains(const WorkspaceState *state, Window window)
{
    if (workspace_find(state, window) >= 0)
        return true;
    for (const PendingWindow *pending = state->pending_head; pending; pending = pending->next) {
        if (pending->window == window)
            return true;
    }
    return false;
}

bool workspace_select(WorkspaceState *state, int target)
{
    if (!valid_workspace(target))
        return false;
    state->active[workspace_monitor(state, target)] = target;
    state->focused = target;
    return true;
}

bool workspace_focus_monitor(WorkspaceState *state, int monitor)
{
    if (monitor < 0 || monitor >= state->monitor_count)
        return false;
    state->focused = state->active[monitor];
    return true;
}

bool workspace_move(WorkspaceState *state, int target)
{
    int source = state->focused;
    if (!valid_workspace(target) || target == source || state->windows[source] == None)
        return false;
    Window window = state->windows[source];
    state->windows[source] = state->windows[target];
    state->windows[target] = window;
    workspace_select(state, target);
    fill_pending(state);
    return true;
}

void workspace_set_monitor_count(WorkspaceState *state, int monitor_count)
{
    state->monitor_count = monitor_count >= 2 ? 2 : 1;
    for (int mon = 0; mon < state->monitor_count; mon++) {
        if (workspace_monitor(state, state->active[mon]) != mon)
            state->active[mon] = mon == 0 ? EXTERNAL_WS_COUNT : 0;
    }
    /* Preserve the focused workspace across hotplug, even when it changes
     * monitors. Both layout and focus will consume this same selection. */
    workspace_select(state, state->focused);
    fill_pending(state);
}

WorkspaceAdmission workspace_admit(WorkspaceState *state, Window window, bool activate)
{
    if (workspace_contains(state, window))
        return WORKSPACE_KNOWN;
    int monitor = workspace_monitor(state, state->focused);
    int target = empty_workspace(state, monitor);
    if (target < 0) {
        PendingWindow *pending = malloc(sizeof(*pending));
        if (!pending)
            return WORKSPACE_NO_MEMORY;
        *pending = (PendingWindow){ .window = window, .preferred_monitor = monitor };
        if (state->pending_tail)
            state->pending_tail->next = pending;
        else
            state->pending_head = pending;
        state->pending_tail = pending;
        return WORKSPACE_QUEUED;
    }
    state->windows[target] = window;
    if (activate)
        workspace_select(state, target);
    return WORKSPACE_ADMITTED;
}
