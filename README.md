# gowm

A very simple xorg window manager I wrote for myself.

## Build

Requires a C compiler, `make`, `pkg-config`, and development packages for Xlib,
Xinerama, Xrandr, and Xft. Install a fontconfig-compatible font such as Noto Sans
for the overview text.

```sh
make
sudo make install
```

Restart gowm in your X session after installing. Exiting gowm closes its managed
applications, so save your work first.

## Workspaces and monitors

Each workspace holds one window. With two monitors, workspaces 1–6 use the
external monitor and 7–9 use the main monitor. With one monitor, all nine use
that monitor. The main output preference is `eDP-1`, then the XRandR primary
output, then an internal panel. At most two monitors are used.

New windows use the focused workspace if it is empty, otherwise the first empty
workspace on that monitor. If that monitor is full, new windows wait unmapped
in a FIFO queue instead of replacing an existing window. They fill empty slots
when a window closes, withdraws, or moves to another monitor. Filling a hidden
workspace does not change focus. Waiting windows for a disconnected external
monitor can use free slots on the remaining monitor. Queued applications are
also closed when gowm exits.

## Keyboard shortcuts

| Shortcut | Action |
| --- | --- |
| Ctrl+W | Open the workspace overview |
| Ctrl+1–9 | Switch workspace |
| Ctrl+Shift+1–9 | Move the current window to a workspace, swapping if occupied |
| Ctrl+Shift+Q | Close the current window |
| Ctrl+Shift+C | Exit gowm |

Ctrl+W is reserved globally, replacing applications' usual close-tab shortcut.

## Workspace overview

Ctrl+W opens a fixed 3×3 grid on the focused monitor. Black background, white
outlines and text. Cards show a workspace number, application name, and window
title. Empty cards show only their number. The current workspace has a thicker
border.

Press **1–9** to switch or **Escape** to close. Numeric keypad digits also work
with Num Lock enabled.

## Source layout

- `gowm.c`: startup, event dispatch, actions, and X11 layout/focus updates.
- `workspace.c` / `workspace.h`: workspace assignments, selections, swaps, and
  the pending-window queue. State transitions make no Xlib calls.
- `monitors.c` / `monitors.h`: monitor discovery and geometry, without workspace
  assignment policy.
- `ui.c` / `ui.h`: indicator and overview drawing, modal grabs, and UI resources.
- `bindings.h`: one keybinding table used for both grabs and action dispatch.
- `config.h`: workspace counts, monitor preferences, and overview columns.

Actions finish changing workspace state before the controller updates layout,
UI, and focus. Only layout maps managed client windows; focusing never maps a
hidden client. The focused workspace must be active on its assigned monitor.

To add an action, add its handler and binding without duplicating key grabs.
Keep placement and workspace policy in `workspace.c`, not in X11 event handlers.
The fixed nine-workspace keyboard scheme and two-monitor policy are intentional;
changing those limits also requires updating their bindings and policy.

## Tests

```sh
make test        # State tests and isolated X11 integration tests
make test-state  # No running X server needed
make test-x11    # Real WM on a private Xvfb server
```

State tests cover same-monitor and cross-monitor swaps, monitor connection
changes, placement, remapping known windows, and FIFO queue promotion/removal.
The integration suite never uses your active display. In addition to the build
dependencies, it requires `Xvfb`, `timeout`, and XTest development headers.
Real X11 coverage uses one monitor; multi-monitor state transitions are tested
through the workspace API.
