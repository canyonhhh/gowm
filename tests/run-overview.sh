#!/bin/sh
set -eu

# Never inherit the desktop display, even if Xvfb fails to start.
unset DISPLAY
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
wm=${1:-./gowm}
case "$wm" in /*) ;; *) wm="$PWD/$wm" ;; esac
[ -x "$wm" ] || { printf 'Not executable: %s\n' "$wm" >&2; exit 1; }
tmp=$(mktemp -d)
xvfb_pid=
wm_pid=
cleanup() {
    if [ -n "$wm_pid" ]; then kill "$wm_pid" 2>/dev/null || :; wait "$wm_pid" 2>/dev/null || :; fi
    if [ -n "$xvfb_pid" ]; then kill "$xvfb_pid" 2>/dev/null || :; wait "$xvfb_pid" 2>/dev/null || :; fi
    rm -rf -- "$tmp"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# Word splitting is intentional for compiler commands and flag lists.
${CC:-cc} ${CPPFLAGS:-} ${CFLAGS:--std=c11 -O2 -Wall -Wextra -Wpedantic} \
    $(pkg-config --cflags x11 xtst) "$root/tests/test_x11.c" \
    ${LDFLAGS:-} $(pkg-config --libs x11 xtst) -o "$tmp/test_x11"
Xvfb -displayfd 3 -screen 0 1024x768x24 -nolisten tcp \
    3>"$tmp/display" >"$tmp/xvfb.log" 2>&1 &
xvfb_pid=$!
i=0
while [ ! -s "$tmp/display" ]; do
    if ! kill -0 "$xvfb_pid" 2>/dev/null || [ "$i" -ge 500 ]; then
        printf 'Xvfb did not become ready\n' >&2
        exit 1
    fi
    i=$((i + 1))
    sleep 0.01
done
read -r display < "$tmp/display"
case "$display" in ''|*[!0-9]*) printf 'Invalid Xvfb display\n' >&2; exit 1;; esac
DISPLAY=:$display
export DISPLAY

status=0
for scenario in ${GOWM_TESTS:-switch swap overview lifecycle overview-locks caps-shortcuts num-shortcuts both-shortcuts button-shortcuts queue remap}; do
    "$wm" >"$tmp/wm.log" 2>&1 &
    wm_pid=$!
    if ! timeout 15s "$tmp/test_x11" "$scenario"; then
        printf 'FAIL: %s\n' "$scenario" >&2
        # Preserve diagnostics in the output before deleting temporary files.
        while IFS= read -r line; do printf '  WM: %s\n' "$line" >&2; done < "$tmp/wm.log"
        status=1
    fi
    kill "$wm_pid" 2>/dev/null || :
    wait "$wm_pid" 2>/dev/null || :
    wm_pid=
done
exit "$status"
