# Notices: ToyStory2Recomp

ToyStory2Recomp is an unofficial, noncommercial fan project. This repository
combines material of several origins under different terms. This file states
which terms apply to which files. It does not grant any rights in the original
game, and neither it nor any other notice in this repository creates a right
to redistribute game code.

Provenance below was determined from the Git history of this repository and by
comparing files with the psxrecomp scaffold templates at the pinned psxrecomp
commit. Where ownership is inferred rather than documented, this file says so.

## 1. Original project code: PolyForm Noncommercial 1.0.0

Copyright (c) 2026 PeriBluGaming. Licensed under the PolyForm Noncommercial
License 1.0.0 (see `LICENSE`).

- `mods/src/bios_free_compat.c`: BIOS `free()` compatibility for OpenBIOS
- `mods/src/modern_camera.c`: Modern Camera
- `mods/src/skip_startup_intro.c`: Skip Startup Intro
- `mods/src/widescreen.c`: native 16:9 widescreen
- `mods/preloaded/packages/*/*/manifest.toml`: mod package manifests
- `tools/apply_psxrecomp_patch.sh`: patch-series helper

The `.c` and `.sh` files above carry
`SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0`.

The mods are written against psxrecomp's mod API and are compiled into a
runtime that is itself under psxrecomp's terms (section 6). Game addresses and
behaviour described in them are facts about the original game; no claim is
made to those facts.

## 2. Mixed-origin files: psxrecomp-derived with project changes

These files were generated from psxrecomp's New Project Layout templates or
modify psxrecomp source. Their upstream portions are Copyright (c) 2026
Matthew Stanley, under psxrecomp's PolyForm Noncommercial License 1.0.0
(copy in `docs/licenses/psxrecomp-LICENSE`). PeriBluGaming's changes are
offered under the same license. Each file as a whole remains subject to
psxrecomp's terms; PeriBluGaming cannot license the upstream portions.

| Path | Origin |
|---|---|
| `patches/*.patch` | Changes to psxrecomp source, including upstream context lines |
| `CMakeLists.txt`, `game.toml`, `README.md` | Scaffold template plus substantial project changes |
| `.github/workflows/release.yml`, `scripts/package_release.sh` | Scaffold template plus project changes |
| `codegen_setup.c`, `codegen_setup.h`, `symbols.toml`, `game_options.toml`, `.gitignore` | Scaffold template, mostly token substitution |

`game.toml` largely records factual addresses in the original game; no claim
is made to those facts. The `retcomm-readme-*` blocks in `README.md` are
maintained by upstream tooling.

## 3. Unmodified upstream files

Byte-identical to, or generated unchanged from, psxrecomp. Under psxrecomp's
terms, Copyright (c) 2026 Matthew Stanley, unless stated otherwise:

- `tools/sync_symbols.py`
- `assets/psxrecomp.ico`, `assets/psxrecomp.png`, `assets/psxrecomp.svg`
  (default app icon)
- `mods/preloaded/README.md` (scaffold text written by psxrecomp's
  `setup_project.sh`)
- `.github/raid-discord.png` (R.A.I.D. community branding, shipped by the
  psxrecomp scaffold)
- `docs/licenses/psxrecomp-LICENSE` (verbatim copy of `psxrecomp/LICENSE`)

The icon and `raid-discord.png` carry no notice of their own; their terms are
inferred from psxrecomp's repository-wide license.

## 4. Game code, data and assets: not licensed

*Toy Story 2: Buzz Lightyear to the Rescue!* (SLUS-00893) and all of its
code, data, audio, video, characters and artwork belong to their respective
rights holders. PeriBluGaming does not own them, and the license in `LICENSE`
does not cover them.

- `generated/`: C code that PSXRecomp produced by machine translation of the
  game's retail executable. It is derived from copyrighted game code. No
  license to it is granted by this project. The same applies to the compiled
  game inside release executables.
- `docs/ToyStory2Recomp.png`: a screenshot of the game running in this
  project, shown for identification only.

Users are responsible for obtaining and using a lawful copy of the original
game. Disc images and BIOS dumps are never
included.

## 5. Tool-generated metadata

`seeds/ghidra_funcs.txt`, `psx_symbols.h`, `catalog_identity.json`,
`disc_probe.json`, `.recomp.json`, `.gitmodules` and `framework_pins.txt` were
produced by psxrecomp tooling or the project scaffold and record facts about
the game, the disc or the build. PeriBluGaming claims no copyright in them.

## 6. Submodules and third-party components

| Component | Terms | Where |
|---|---|---|
| [psxrecomp](https://github.com/mstan/psxrecomp), pinned at `7cddab8237e1f73b7067b8401af602d90a96b80d` | PolyForm Noncommercial 1.0.0, Copyright (c) 2026 Matthew Stanley | `psxrecomp/LICENSE`; copy in `docs/licenses/psxrecomp-LICENSE` |
| [recomp-ui](https://github.com/RetroPortingToolKit/recomp-ui), pinned at `0546fbcb0126bb14bf88d2128b4fe8a4229570a3` | MIT, Copyright (c) 2026 Matthew Stanley | `recomp-ui/LICENSE` |
| Components bundled by psxrecomp (OpenBIOS, libchdr, libwebp, xxHash, toml11, stb_image, TinyCC and others) | Their own licenses | `psxrecomp/THIRD_PARTY_ATTRIBUTION.md`, `psxrecomp/runtime/licenses/` |
| Components bundled by recomp-ui (Dear ImGui, stb, fonts under OFL-1.1 and CC BY-SA 4.0) | Their own licenses | `recomp-ui/src/third_party/`, `recomp-ui/assets/common/*/NOTICE.md` |

Source archives downloaded from GitHub do not contain submodule files. Fetch
them with `git submodule update --init --recursive`.

Release ZIP archives ship this project's `LICENSE` and this `NOTICE.md` at
their root. They also carry the upstream notices: `licenses/` holds psxrecomp's
license (`psxrecomp-LICENSE`), its third-party attribution and notices, and
recomp-ui's license; `bios/OpenBIOS.LICENSE`, `assets/fonts/NOTICE.md` and
`assets/img/NOTICE.md` sit beside the files they cover. The executable
contains the compiled game (section 4), the psxrecomp runtime and recomp-ui,
and this project's mods (section 1). Each part keeps its own terms.

## 7. Attribution

If you redistribute this project's code, the license requires you to include
its terms (or their URL) and every `Required Notice:` line in `LICENSE`, and
to keep the notices of psxrecomp and the other components. That is the full
attribution requirement. A visible credit in a README, store page or in the
app is appreciated but not required.

## 8. Commercial use

No license for commercial use is granted. Commercial use of this project's
own code requires separate written permission from PeriBluGaming. Such
permission could cover only PeriBluGaming's own code. It cannot cover
psxrecomp (licensed by Matthew Stanley), other third-party components, or
the game.

## 9. AI-assisted development

Parts of the original code and documentation were written with AI coding
assistants (GitHub Copilot, Claude) under PeriBluGaming's direction, review
and integration. Copyright is claimed only to the extent the law recognises
it. The license applies to whatever protectable authorship these files
contain.

## 10. Trademarks and affiliation

This project is not affiliated with, endorsed, sponsored or approved by
Disney, Pixar, Activision, Traveller's Tales or Sony Interactive
Entertainment. All names and trademarks belong to their respective owners.
