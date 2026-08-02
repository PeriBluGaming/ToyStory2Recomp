# SLUS-00893

Generated locally by PSXRecomp from your own disc and BIOS.

## Build

Install CMake, Ninja, a C/C++ compiler, and SDL2 development files.
Then run `sh build.sh` on macOS/Linux or `.\build.ps1` in PowerShell.

The executable is written under `build/`. Keep your original disc image
at the path stored in `game.toml`, or update that path before running.

The `input/`, `generated/`, and `bios-generated/` folders contain data
derived from copyrighted files you supplied. Do not redistribute them.
