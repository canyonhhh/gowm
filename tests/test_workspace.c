#include <assert.h>
#include <stdio.h>
#include "../workspace.h"

static void occupied_swap_keeps_destination_active(void)
{
    WorkspaceState state;
    workspace_init(&state, 1);
    assert(workspace_admit(&state, 101, true) == WORKSPACE_ADMITTED);
    assert(workspace_admit(&state, 102, true) == WORKSPACE_ADMITTED);
    assert(workspace_select(&state, 0));
    assert(workspace_move(&state, 1));
    assert(state.windows[0] == 102);
    assert(state.windows[1] == 101);
    assert(state.focused == 1);
    assert(state.active[0] == 1);
    workspace_destroy(&state);
}

static void overflow_waits_without_replacing_clients(void)
{
    WorkspaceState state;
    workspace_init(&state, 1);
    for (Window window = 1; window <= 9; window++)
        assert(workspace_admit(&state, window, true) == WORKSPACE_ADMITTED);
    assert(workspace_admit(&state, 10, true) == WORKSPACE_QUEUED);
    assert(workspace_admit(&state, 11, true) == WORKSPACE_QUEUED);
    assert(state.windows[8] == 9);
    assert(state.focused == 8);
    assert(workspace_contains(&state, 10));
    assert(workspace_find(&state, 10) == -1);
    assert(workspace_remove(&state, 9));
    assert(state.windows[8] == 10);
    assert(state.focused == 8);
    assert(workspace_remove(&state, 3));
    assert(state.windows[2] == 11);
    assert(state.focused == 8); /* Filling a hidden slot must not steal focus. */
    assert(!workspace_contains(&state, 9));
    assert(workspace_find(&state, 10) == 8);
    assert(workspace_find(&state, 11) == 2);
    workspace_destroy(&state);
}

static void moving_to_another_monitor_promotes_waiting_client(void)
{
    WorkspaceState state;
    workspace_init(&state, 2);
    for (Window window = 1; window <= 6; window++)
        assert(workspace_admit(&state, window, true) == WORKSPACE_ADMITTED);
    assert(workspace_admit(&state, 7, true) == WORKSPACE_QUEUED);
    assert(workspace_move(&state, 6));
    assert(state.windows[6] == 6);
    assert(state.windows[5] == 7);
    assert(state.active[0] == 6);
    assert(state.active[1] == 5);
    assert(state.focused == 6);
    workspace_destroy(&state);
}

static void monitor_changes_preserve_focus_and_drain_queue(void)
{
    WorkspaceState state;
    workspace_init(&state, 1);
    workspace_select(&state, 4);
    workspace_set_monitor_count(&state, 2);
    assert(state.focused == 4);
    assert(state.active[1] == 4);
    assert(state.active[0] == 6);
    workspace_set_monitor_count(&state, 1);
    assert(state.active[0] == 4);
    workspace_set_monitor_count(&state, 2);
    for (Window window = 1; window <= 6; window++)
        assert(workspace_admit(&state, window, true) == WORKSPACE_ADMITTED);
    assert(workspace_admit(&state, 7, true) == WORKSPACE_QUEUED);
    workspace_set_monitor_count(&state, 1);
    assert(workspace_find(&state, 7) == 6);
    assert(state.focused == 5);
    assert(state.active[0] == 5);
    workspace_destroy(&state);
}

static void known_window_keeps_its_assignment(void)
{
    WorkspaceState state;
    workspace_init(&state, 1);
    workspace_admit(&state, 101, true);
    workspace_select(&state, 8);
    assert(workspace_admit(&state, 101, true) == WORKSPACE_KNOWN);
    assert(workspace_find(&state, 101) == 0);
    assert(state.focused == 8);
    assert(state.active[0] == 8);
    workspace_destroy(&state);
}

static void overview_admission_does_not_switch_workspace(void)
{
    WorkspaceState state;
    workspace_init(&state, 1);
    workspace_admit(&state, 101, true);
    assert(workspace_admit(&state, 102, false) == WORKSPACE_ADMITTED);
    assert(workspace_find(&state, 102) == 1);
    assert(state.focused == 0);
    assert(state.active[0] == 0);
    workspace_destroy(&state);
}

static void occupied_cross_monitor_swap_preserves_both_selections(void)
{
    WorkspaceState state;
    workspace_init(&state, 2);
    workspace_admit(&state, 101, true);
    workspace_select(&state, 6);
    workspace_admit(&state, 102, true);
    assert(workspace_move(&state, 0));
    assert(state.windows[0] == 102);
    assert(state.windows[6] == 101);
    assert(state.active[0] == 6);
    assert(state.active[1] == 0);
    assert(state.focused == 0);
    assert(workspace_focus_monitor(&state, 0));
    assert(state.focused == 6);
    workspace_destroy(&state);
}

static void queues_are_fifo_per_monitor_and_can_be_cancelled(void)
{
    WorkspaceState state;
    workspace_init(&state, 2);
    for (Window window = 1; window <= 6; window++)
        workspace_admit(&state, window, true);
    assert(workspace_admit(&state, 10, true) == WORKSPACE_QUEUED);
    assert(workspace_admit(&state, 10, true) == WORKSPACE_KNOWN);
    assert(workspace_admit(&state, 11, true) == WORKSPACE_QUEUED);
    assert(workspace_admit(&state, 12, true) == WORKSPACE_QUEUED);
    assert(workspace_remove(&state, 11)); /* Remove a middle node. */
    assert(workspace_remove(&state, 12)); /* Remove the tail before appending. */
    assert(workspace_admit(&state, 13, true) == WORKSPACE_QUEUED);
    workspace_select(&state, 6);
    for (Window window = 7; window <= 9; window++)
        workspace_admit(&state, window, true);
    assert(workspace_admit(&state, 14, true) == WORKSPACE_QUEUED);
    assert(workspace_remove(&state, 9));
    assert(state.windows[8] == 14); /* External queue must not block main. */
    assert(workspace_find(&state, 10) == -1);
    assert(workspace_remove(&state, 1));
    assert(state.windows[0] == 10);
    assert(workspace_remove(&state, 2));
    assert(state.windows[1] == 13);
    assert(!workspace_contains(&state, 11));
    assert(!workspace_contains(&state, 12));
    assert(!workspace_remove(&state, 99));
    /* All queued nodes have been consumed. Appending must still work. */
    assert(workspace_admit(&state, 15, true) == WORKSPACE_QUEUED);
    assert(workspace_remove(&state, 15));
    assert(workspace_admit(&state, 16, true) == WORKSPACE_QUEUED);
    workspace_destroy(&state);
}

static void invalid_actions_leave_selection_alone(void)
{
    WorkspaceState state;
    workspace_init(&state, 1);
    assert(!workspace_select(&state, -1));
    assert(!workspace_select(&state, 9));
    assert(!workspace_focus_monitor(&state, 1));
    assert(!workspace_focus_monitor(&state, -1));
    assert(!workspace_move(&state, 1));
    assert(workspace_find(&state, None) == -1);
    assert(!workspace_remove(&state, None));
    assert(state.focused == 0);
    assert(state.active[0] == 0);
    workspace_destroy(&state);
}

int main(void)
{
    occupied_swap_keeps_destination_active();
    overflow_waits_without_replacing_clients();
    moving_to_another_monitor_promotes_waiting_client();
    monitor_changes_preserve_focus_and_drain_queue();
    known_window_keeps_its_assignment();
    overview_admission_does_not_switch_workspace();
    occupied_cross_monitor_swap_preserves_both_selections();
    queues_are_fifo_per_monitor_and_can_be_cancelled();
    invalid_actions_leave_selection_alone();
    puts("workspace tests passed");
    return 0;
}
