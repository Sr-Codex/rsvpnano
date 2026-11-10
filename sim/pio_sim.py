"""PlatformIO glue for env:native_sim (Lume Nano desktop simulator).

Adds the simulator sources (sim/), the three host-safe Arduino_GFX translation units
(the rest of the library talks to ESP32 buses) and SDL2.
"""
import shutil
import subprocess
from pathlib import Path

Import("env")

project = Path(env.subst("$PROJECT_DIR"))
gfx = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV") / "GFX Library for Arduino" / "src"

env.Prepend(CPPPATH=[str(project / "sim" / "support"), str(project / "sim" / "platform")])
env.Append(CPPPATH=[str(gfx)])
# sim/ sources are built outside the LDF graph; give them the project libraries' headers.
libdeps = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV")
env.Append(CPPPATH=[
    str(project / "lib/HarfBuzz/upstream/src"), str(project / "lib/SheenBidi/upstream/Headers"),
    str(project / "lib/PDFio/upstream"), str(project / "lib/zlib/upstream"),
    str(libdeps / "glaze" / "include"), str(libdeps / "JPEGDEC" / "src"), str(libdeps / "Improv" / "src"),
])

sdl_config = shutil.which("sdl2-config")
if not sdl_config:
    raise RuntimeError("SDL2 not found: brew install sdl2 (macOS) / apt install libsdl2-dev (Linux)")
env.ParseConfig(f"{sdl_config} --cflags --libs")

env.BuildSources(
    "$BUILD_DIR/gfx", str(gfx),
    src_filter=["-<*>", "+<Arduino_G.cpp>", "+<Arduino_GFX.cpp>", "+<canvas/Arduino_Canvas.cpp>"],
)
env.BuildSources(
    "$BUILD_DIR/sim", str(project / "sim"),
    src_filter=["+<*.cpp>", "+<platform/*.cpp>", "+<stubs/*.cpp>", "+<support/*.cpp>"],
)
