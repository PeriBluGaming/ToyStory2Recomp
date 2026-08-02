# Toy Story 2 - Buzz Lightyear to the Rescue Recomp
> **These are in-development previews, not finished ports — expect rough
> edges**, and depth will keep landing over months, not days. My time for any
> one title is limited, so I ask for your patience. Contributions are welcome —
> testing, issues, and PRs to the game or framework all help and will

Toy Story 2: Buzz Lightyear to the Rescue (USA, SLUS-00893) statically recompiled to a native PC executable with
[PSXRecomp](https://github.com/mstan/psxrecomp).

## What This Is

This repository contains the game-specific configuration, seeds, tools, and
build glue for running Tomba on the PSXRecomp framework. Tomba's MIPS code is
machine-translated ("recompiled") ahead of time into native C, then compiled
into a real Windows/macOS/Linux program that runs the game's own logic on a
faithful simulation of the PS1 hardware (GPU, SPU, GTE, memory cards) plus the
real, recompiled PS1 BIOS — no high-level emulation shims.

It does **not** contain the Tomba disc image, a retail PS1 BIOS, generated game
code, or any decompiled Tomba C. Release builds include the MIT-licensed
OpenBIOS from PCSX-Redux; game data and an optional retail BIOS come from your
own legally obtained assets.

Important files:

- `game.toml`: Toy Story 2 runtime / recompiler / video / controller / widescreen config.
- `seeds/`: Ghidra-derived function starts and game-specific seed data.
- `tools/regen.sh` (macOS/Linux) / `tools/regen.ps1` (Windows): regenerates the
  Toy Story 2 recompiled C output by invoking the framework recompiler.
- `tools/package_release.ps1`: builds the redistributable release zip.
- `psxrecomp/`: the [PSXRecomp](https://github.com/mstan/psxrecomp) framework,
  pulled in as a **git submodule** pinned to a known-good commit.

## Status
The game is playable from BIOS boot through gameplay. Latest release: **xxx**

| Area | State |
|---|---|
| PS1 BIOS boot | Works (real recompiled BIOS) |
| Disc-detect / license screen | Works |
| Whoopee Camp + intro FMV | Works not for now |
| Title menu / OPTIONS | Works (settings persist across launches) |
| NEW GAME / LOAD GAME | Works |
| Memory-card save & load | Works (standard `.mcd`, emulator-compatible) |
| Gameplay | Playable; known crashes tracked in `ISSUES.md` |
| Renderers | Software **and** OpenGL (GPU); OpenGL is the default |
| Widescreen 16:9 | Experimental, opt-in (true wider FOV) |
| Controller | Analog / D-Pad modes; optional Special Edition Hybrid mod |

## Features
- Two renderers. A CPU software rasterizer and a GPU-authoritative OpenGL backend (default). OpenGL moves rasterization and supersampling onto the GPU so fill-heavy scenes (e.g. the mushroom forest) hold 59.94 fps. Falls back to software automatically if GL init fails.
- Supersampling + anti-aliasing. Internal-resolution SSAA (2×–4×) with optional linear present filtering for clean edges. Ships at 2×.
- Supersampling + anti-aliasing. Internal-resolution SSAA (2×–4×) with optional linear present filtering for clean edges. Ships at 2×.
- Self-growing native cache. Areas you visit are converted to fast native code as you play and reused on later launches (see "Help make your game faster" below).

## Setup
### Release Package (recommended)
1. Download ToyStory2Recomp-v*-windows-x64.zip from Releases and extract it.
2. Run ToyStory2Recomp.exe. A launcher window opens.
3. Select your legally obtained SCPH1001.BIN in the BIOS row.
4. Set the game disc: select your legally obtained Toy Story 2: Buzz Lightyear to the Rescue (USA, SLUS-00893 disc image. The launcher verifies the ISO9660 header, region, and serial.
5. Optionally adjust renderer, supersampling, screen look, and controller settings. 

Accepted disc formats: .cue + .bin (preferred — pick the .cue), direct .bin, and .iso. If the header or game ID does not match SCUS-94236, the launcher warns and tries to run the image anyway.

