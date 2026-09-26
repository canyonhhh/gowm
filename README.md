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

## Tests

```sh
make test
```

The integration suite starts a private Xvfb server and tests the real window
manager with nine clients. It never uses your active display. In addition to the
build dependencies, it requires `Xvfb`, `timeout`, and XTest development headers.
Tests cover switching, cancellation, focus, keyboard and pointer grabs, title
updates, window creation/destruction, and Caps Lock/Num Lock. Automated coverage
uses one monitor.
