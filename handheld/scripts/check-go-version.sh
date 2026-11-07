#!/bin/sh
# Checks that the device build's Go (ARG GO_VERSION in the Containerfile) is
# the version mise.toml pins (go = "..."), so the device and host builds use
# the same toolchain. Usage: check-go-version.sh CONTAINERFILE MISE_TOML
set -eu
containerfile=$1
mise=$2
image=$(sed -n 's/^ARG GO_VERSION=\([0-9][0-9.]*\)[[:space:]]*$/\1/p' "$containerfile")
pinned=$(sed -n 's/^go = "\([0-9][0-9.]*\)"[[:space:]]*$/\1/p' "$mise")
if [ -z "$image" ] || [ -z "$pinned" ]; then
  echo "check-go-version: no Go version found (Containerfile: '$image', mise.toml: '$pinned')" >&2
  exit 1
fi
if [ "$image" != "$pinned" ]; then
  echo "check-go-version: the Containerfile has Go $image, mise.toml pins $pinned" >&2
  exit 1
fi
echo "check-go-version: Go $image"
