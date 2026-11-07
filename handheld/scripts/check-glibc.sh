#!/bin/sh
# Checks the device binary (design D12): an AArch64 ELF whose highest required
# GLIBC symbol version is at most 2.31 (bullseye's; the TrimUI has 2.33).
# Reads the host's readelf: -h for the machine, -V for the version needs. The
# two checks are separate, so neither can pass behind the other.
# Usage: check-glibc.sh BINARY
set -eu
bin=$1
limit=2.31

machine=$(readelf -h "$bin" | sed -n 's/^[[:space:]]*Machine:[[:space:]]*//p')
case "$machine" in
  AArch64) ;;
  *)
    echo "check-glibc: $bin is for '$machine', not AArch64" >&2
    exit 1
    ;;
esac

needs=$(readelf -V "$bin" | grep -o 'GLIBC_[0-9][0-9.]*' | sed 's/^GLIBC_//' | sort -u -V)
highest=$(printf '%s\n' "$needs" | tail -n 1)
if [ -n "$highest" ] && [ "$(printf '%s\n%s\n' "$limit" "$highest" | sort -V | tail -n 1)" != "$limit" ]; then
  echo "check-glibc: $bin needs GLIBC_$highest, above GLIBC_$limit" >&2
  exit 1
fi
echo "check-glibc: $bin is AArch64, highest GLIBC need ${highest:+GLIBC_}${highest:-none} (at most GLIBC_$limit)"
