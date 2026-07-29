# Toy Story 2 Recomp
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
  Tomba recompiled C output by invoking the framework recompiler.
- `tools/package_release.ps1`: builds the redistributable release zip.
- `psxrecomp/`: the [PSXRecomp](https://github.com/mstan/psxrecomp) framework,
  pulled in as a **git submodule** pinned to a known-good commit.