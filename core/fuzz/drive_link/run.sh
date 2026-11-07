#!/bin/sh
# Builds the drive link fuzz driver with the host compiler and runs each target
# for the given time (design D8; `mise run fuzz-link --time 60s`). Adds
# AddressSanitizer and UndefinedBehaviorSanitizer when their runtimes link, and
# warns and continues without them otherwise. Exits non-zero, with the input
# saved to the target's corpus directory, when a target finds a violation.
set -eu

time_arg="${1:-60s}"
case "$time_arg" in
  *ms) echo "fuzz-link: --time takes seconds or minutes (60s, 2m)" >&2; exit 2 ;;
  *s) seconds="${time_arg%s}" ;;
  *m) seconds=$(( ${time_arg%m} * 60 )) ;;
  *) seconds="$time_arg" ;;
esac
case "$seconds" in
  ''|*[!0-9]*) echo "fuzz-link: bad --time $time_arg" >&2; exit 2 ;;
esac

root=$(cd "$(dirname "$0")/../../.." && pwd)
out="$root/build/fuzz-link"
mkdir -p "$out"
cxx="${CXX:-c++}"
src="$root/core/fuzz/drive_link"
sources="$src/driver.cpp $src/targets.cpp $root/core/drive_link/protocol.cpp $root/core/drive_link/session.cpp $root/core/drive_link/link.cpp"

probe="$out/probe.cpp"
echo 'int main() { return 0; }' > "$probe"
sanitize=""
for flags in "-fsanitize=address,undefined -fno-sanitize-recover=undefined" "-fsanitize=undefined -fno-sanitize-recover=undefined"; do
  # shellcheck disable=SC2086
  if "$cxx" $flags "$probe" -o "$out/probe" >/dev/null 2>&1; then
    sanitize="$flags"
    break
  fi
done
if [ -z "$sanitize" ]; then
  echo "fuzz-link: warning: $cxx cannot link the AddressSanitizer or UndefinedBehaviorSanitizer runtime; running without sanitisers" >&2
else
  echo "fuzz-link: sanitisers: $sanitize"
fi

# shellcheck disable=SC2086
"$cxx" -std=gnu++17 -O1 -g -fno-omit-frame-pointer -Wall -Wextra -Werror $sanitize \
  -I"$root/core" -I"$src" $sources -o "$out/driver"
"$out/driver" --corpus "$src/corpus" --seconds "$seconds"
