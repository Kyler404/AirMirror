"""Build the vcxproj by invoking MSBuild directly, skipping cmake.

cmake --build spawns MSBuild through its own environment handling, which
re-introduces a second, differently-cased PATH key; MSBuild's
ProcessStartInfo.EnvironmentVariables then dies with MSB6001 before CL runs.
Calling MSBuild straight from a de-duplicated environment avoids that layer.
"""

import os
import subprocess
import sys

MSBUILD = (r"C:\Program Files\Microsoft Visual Studio\18\Community"
           r"\MSBuild\Current\Bin\MSBuild.exe")
PROJ = (r"C:\Users\ABC\.zcode\workspace"
        r"\draft-061cf45f-6bb6-45e6-b074-9689fd7ea461\mirror-launcher"
        r"\build\AirPlay-Windows\build\out\airplay-windows.vcxproj")

env = dict(os.environ)
if "Path" not in env:
    for k in [k for k in env if k.upper() == "PATH"]:
        env["Path"] = env.pop(k)

print("MSBuild:", MSBUILD, "exists:", os.path.exists(MSBUILD))
print("project:", PROJ, "exists:", os.path.exists(PROJ))
print("PATH keys:", [k for k in env if k.upper() == "PATH"])

proc = subprocess.run(
    [MSBUILD, PROJ, "/p:Configuration=Release", "/m", "/nologo", "/v:m"],
    env=env,
)
print("build exit code:", proc.returncode)
sys.exit(proc.returncode)
