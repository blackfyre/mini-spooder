#!/bin/sh
# Checks the C toolchain and the pkg-config entries that MiniSpooder.pak's cgo
# build needs on Linux (go-sdl2 v0.4.40: sdl, ttf, img and gfx, which gabagool
# imports), and names the Fedora or Debian packages that provide whatever is
# missing.
set -u

[ "$(uname -s)" = Linux ] || exit 0

family=fedora
if [ -r /etc/os-release ]; then
  # shellcheck disable=SC1091
  . /etc/os-release
  case " ${ID:-} ${ID_LIKE:-} " in
    *" debian "* | *" ubuntu "*) family=debian ;;
  esac
fi

missing=""
add() {
  case " $missing " in
    *" $1 "*) ;;
    *) missing="$missing $1" ;;
  esac
}

if [ "$family" = debian ]; then
  cc_pkg=gcc pc_pkg=pkg-config
else
  cc_pkg=gcc pc_pkg=pkgconf-pkg-config
fi
command -v cc >/dev/null 2>&1 || command -v gcc >/dev/null 2>&1 || add "$cc_pkg"
if ! command -v pkg-config >/dev/null 2>&1; then
  add "$pc_pkg"
  pc=false
else
  pc=true
fi

# pkg-config name:Fedora package:Debian package
for entry in \
  sdl2:sdl2-compat-devel:libsdl2-dev \
  SDL2_ttf:SDL2_ttf-devel:libsdl2-ttf-dev \
  SDL2_image:SDL2_image-devel:libsdl2-image-dev \
  SDL2_gfx:SDL2_gfx-devel:libsdl2-gfx-dev; do
  name=${entry%%:*}
  rest=${entry#*:}
  if [ "$family" = debian ]; then pkg=${rest#*:}; else pkg=${rest%%:*}; fi
  if [ "$pc" = false ] || ! pkg-config --exists "$name"; then
    add "$pkg"
  fi
done

if [ -n "$missing" ]; then
  echo "MiniSpooder.pak's SDL2 build needs these packages:" >&2
  if [ "$family" = debian ]; then
    echo "  sudo apt-get install$missing" >&2
  else
    echo "  sudo dnf install$missing" >&2
  fi
  exit 1
fi
