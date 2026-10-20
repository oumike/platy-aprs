# Injects APP_VERSION into the build from the VERSION file (as camillia-mt
# does). Python rather than a `!echo $(cat VERSION)` build flag so the build
# also works from Windows cmd.exe. A missing VERSION leaves version.h's default.
Import("env")  # noqa: F821  - injected by PlatformIO/SCons

import os

path = os.path.join(env.subst("$PROJECT_DIR"), "VERSION")  # noqa: F821
try:
    with open(path, "r", encoding="utf-8") as fh:
        version = fh.read().strip()
except OSError:
    version = ""
if version:
    env.Append(CPPDEFINES=[("APP_VERSION", env.StringifyMacro(version))])  # noqa: F821
    print("[set_app_version] APP_VERSION=%s" % version)
else:
    print("[set_app_version] VERSION file missing or empty; using version.h's default")
