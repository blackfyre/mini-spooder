# PlatformIO pre: script (web-portal design decision 2): runs
# tools/portal/gen_pages.py before every build, writing portal_pages.h into
# $BUILD_DIR/portal, and adds that directory to the include path. The generator
# rewrites the header only when a page source, the tokens or the labels change,
# so an unchanged portal does not rebuild portal.cpp.
import os
import subprocess

Import("env")  # noqa: F821  (provided by PlatformIO)

project = env.subst("$PROJECT_DIR")  # noqa: F821
out = os.path.join(env.subst("$BUILD_DIR"), "portal")  # noqa: F821
gen = os.path.join(os.path.dirname(project), "tools", "portal", "gen_pages.py")
result = subprocess.run([env.subst("$PYTHONEXE"), "-I", gen, "--out", out])  # noqa: F821
if result.returncode != 0:
    print("portal_pages: the page generator failed")
    env.Exit(1)  # noqa: F821
env.Append(CPPPATH=[out])  # noqa: F821
