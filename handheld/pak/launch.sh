#!/bin/sh
# MiniSpooder.pak's launch script: NextUI runs it from Tools/tg5040/MiniSpooder.pak/
# (openspec change handheld-pak, design D6). It reads wlan0's WiFi
# power-saving setting, runs minispooder --device (which turns power
# saving off for the run and restores it itself), and restores the setting
# however the binary ends: a normal exit, an error, a crash, or a signal to
# this script, which also stops the binary and any process it left running.
# Restoring twice is harmless.

PAK_DIR=$(cd "$(dirname "$0")" && pwd)
cd "$PAK_DIR" || exit 1
PATH="$PAK_DIR:$PATH"
# The device's own libraries win; the pak's (libSDL2_gfx) fill the gap.
export LD_LIBRARY_PATH="/usr/trimui/lib:/usr/lib:$PAK_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
if [ -n "${LOGS_PATH:-}" ] && [ -d "$LOGS_PATH" ]; then
  exec >>"$LOGS_PATH/MiniSpooder.txt" 2>&1
fi

# "Power save: on" or "Power save: off"; anything else leaves the setting
# alone.
saved=$(iw dev wlan0 get power_save 2>/dev/null | sed -n -E 's/^.*: *(on|off) *$/\1/p')
group=""

# stop ends every process the binary started, and the binary if it still
# runs: the binary leads a process group of its own (setsid below), so one
# signal to the group reaches its children and theirs, whether the binary
# has exited, crashed or is still running.
stop() {
  [ -n "$group" ] || return 0
  # "kill -SIG -PGID", without "--": dash and BusyBox ash take no "--" there.
  kill -TERM "-$group" 2>/dev/null || kill -TERM "$group" 2>/dev/null
  # A binary that ignores SIGTERM or hangs on its way out gets SIGKILL after
  # STOP_GRACE seconds, so the setting is restored all the same. The
  # watchdog leads a process group of its own too, its sleep in it, and holds
  # none of the script's output.
  watch='sleep "$1"; kill -KILL "-$2" || kill -KILL "$2"'
  if command -v setsid >/dev/null 2>&1; then
    setsid sh -c "$watch" sh "${STOP_GRACE:-2}" "$group" </dev/null >/dev/null 2>&1 &
  else
    sh -c "$watch" sh "${STOP_GRACE:-2}" "$group" </dev/null >/dev/null 2>&1 &
  fi
  watchdog=$!
  wait "$group" 2>/dev/null
  # Cancelled once the binary has gone, with SIGKILL (dash can hold a
  # trapped SIGTERM back until a sleep ends): the watchdog first, then its
  # group. Killed before its setsid, it has started nothing; after, its
  # sleep is in the group, which outlives the watchdog for the second kill.
  kill -KILL "$watchdog" 2>/dev/null
  kill -KILL "-$watchdog" 2>/dev/null
  wait "$watchdog" 2>/dev/null
  # Anything of the group that ignored SIGTERM.
  kill -KILL "-$group" 2>/dev/null
  group=""
}

restore() {
  stop
  if [ -n "$saved" ]; then
    iw dev wlan0 set power_save "$saved"
  fi
}
# restore runs once: on the way out, or for a signal, which then exits.
trap restore EXIT
trap 'restore; trap - EXIT; exit 129' HUP
trap 'restore; trap - EXIT; exit 130' INT
trap 'restore; trap - EXIT; exit 143' TERM

# In the background, so that a signal to this script runs its trap at once
# instead of after the binary has exited. setsid makes the binary (same pid:
# a background job of this script is no group leader, so setsid does not
# fork) the leader of a new process group.
if command -v setsid >/dev/null 2>&1; then
  setsid minispooder --device "$@" &
else
  minispooder --device "$@" &
fi
group=$!
wait "$group"
status=$?
exit "$status"
