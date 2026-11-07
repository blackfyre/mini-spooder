# PlatformIO pre: script (design D2): defines MINI_SPOODER_VERSION as the output of
# `git describe --always --dirty`, or "unknown" outside a git checkout.
# The define goes to the project's own sources only (SRC_BUILD_FLAGS, the value of
# build_src_flags), so a new version rebuilds firmware/src/, not the Arduino core.
import subprocess

Import("env")  # noqa: F821  (provided by PlatformIO)


def describe(cwd):
    try:
        out = subprocess.run(
            ["git", "describe", "--always", "--dirty"],
            cwd=cwd,
            capture_output=True,
            text=True,
            check=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"
    return out or "unknown"


version = describe(env.subst("$PROJECT_DIR"))  # noqa: F821
env.Append(SRC_BUILD_FLAGS=['-DMINI_SPOODER_VERSION=\\"%s\\"' % version])  # noqa: F821
print("mini-spooder version: %s" % version)
