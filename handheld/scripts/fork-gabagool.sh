#!/bin/sh
# The patched gabagool MiniSpooder.pak builds against (openspec change
# handheld-pak, design D5): downloads the pinned release through the go
# command, which checks it against the checksum database, checks the module
# hash against the pin below as well, copies it to build/handheld/gabagool and
# applies third_party/gabagool-power-hook.patch (git apply --check first). The
# copy is made beside the target and swapped in at the end, so a failed run
# leaves the previous copy in place and the task can run any number of times.
#
# Run from the repository root (mise run handheld-fork).
set -eu

module=github.com/BrandonKowalski/gabagool/v2
version=v2.26.6
# The go.sum hash of the release (go mod download -json prints it).
sum=h1:CxsnbQ0L3EVgvADH5xO8RbdsjAF1qmClJTsIlb3Pwwg=

root=$(pwd)
patch=$root/handheld/third_party/gabagool-power-hook.patch
out=$root/build/handheld/gabagool
mkdir -p "$root/build/handheld"

# Settings that would skip or weaken the checksum check are cleared for this
# download only.
json=$(cd "$root/build/handheld" && GOFLAGS= GONOSUMDB= GONOSUMCHECK= GOPRIVATE= GOINSECURE= GOSUMDB=sum.golang.org \
  GO111MODULE=on go mod download -json "$module@$version")
field() {
  printf '%s\n' "$json" | sed -n "s/^[[:space:]]*\"$1\": \"\\(.*\\)\",\{0,1\}\$/\\1/p"
}
dir=$(field Dir)
got=$(field Sum)
if [ -z "$dir" ] || [ ! -d "$dir" ]; then
  echo "handheld-fork: go mod download gave no directory for $module@$version" >&2
  exit 1
fi
if [ "$got" != "$sum" ]; then
  echo "handheld-fork: $module@$version has hash $got, expected $sum" >&2
  exit 1
fi

tmp=$(mktemp -d "$root/build/handheld/gabagool.XXXXXX")
trap 'rm -rf "$tmp"' EXIT INT TERM HUP
cp -R "$dir/." "$tmp/"
chmod -R u+w "$tmp"
# git apply takes paths from the top of the enclosing repository and skips,
# silently, those outside the current directory; build/ lies inside this
# repository, so the copy is made a repository of its own for the apply.
(cd "$tmp" && git init -q && git apply --check "$patch" && git apply "$patch" && rm -rf .git)
grep -q 'config.Hook(true)' "$tmp/pkg/gabagool/internal/power.go" || {
  echo "handheld-fork: the patch did not reach pkg/gabagool/internal/power.go" >&2
  exit 1
}
rm -rf "$out"
mv "$tmp" "$out"
trap - EXIT INT TERM HUP
echo "handheld-fork: $module@$version patched in build/handheld/gabagool"
