# ToyStory2 Recompiled

<!-- retcomm-readme-metrics -->
[![GitHub downloads (all assets, all releases)](https://img.shields.io/github/downloads/PeriBluGaming/ToyStory2Recomp/total)](https://github.com/PeriBluGaming/ToyStory2Recomp/releases)
[![GitHub downloads (latest release)](https://img.shields.io/github/downloads/PeriBluGaming/ToyStory2Recomp/latest/total)](https://github.com/PeriBluGaming/ToyStory2Recomp/releases/latest)
[![GitHub release](https://img.shields.io/github/v/release/PeriBluGaming/ToyStory2Recomp)](https://github.com/PeriBluGaming/ToyStory2Recomp/releases/latest)
<!-- /retcomm-readme-metrics -->

Static recompilation of **ToyStory2** built on
[psxrecomp](https://github.com/mstan/psxrecomp) and
[recomp-ui](https://github.com/RetroPortingToolKit/recomp-ui).

Toy Story 2: Buzz Lightyear to the Rescue (USA, SLUS-00893) statically recompiled to a native PC executable with PSXRecomp.
![ToyStory2Recomp](/docs/ToyStory2Recomp.png)

| | |
|---|---|
| Players | 1 |
| Region | USA |
| Publisher | Activision, Traveller's Tales |
| Year | 1999 |

Scaffolded with the New Project Layout. See
`psxrecomp/docs/GAME_PROJECT_SETUP.md` for the full flow.

<!-- retcomm-readme-launcher -->
## Retro Launcher

You can run this title **standalone** (download the release zip, point it at
your disc, play), or manage installs, updates, and disc/BIOS wiring with
**[Retro Launcher](https://github.com/RetroPortingToolKit/Retro-Launcher)** —
the Retro Compilation Manager hub for self-compiling recomps.

[Downloads](https://github.com/RetroPortingToolKit/Retro-Launcher/releases) ·
[Full README & features](https://github.com/RetroPortingToolKit/Retro-Launcher#readme)

<p align="center">
  <img src="https://raw.githubusercontent.com/RetroPortingToolKit/Retro-Launcher/main/docs/screenshots/hub-and-game-launcher.png" alt="Retro hub with a background build, next to a title’s recomp-ui launcher" width="720">
</p>

<p align="center">
  <img src="https://raw.githubusercontent.com/RetroPortingToolKit/Retro-Launcher/main/docs/screenshots/queue-and-background-build.png" alt="Background cmake build with titles queued" width="720">
</p>

Retro checks for updates, installs the prebuilt release zips, and automates
BIOS/ROM/save plumbing so you are not stuck repeating each game’s first run by hand.
<!-- /retcomm-readme-launcher -->

## Legal

You must own the original game. Disc images under `disc/` are gitignored and
must never be committed. Retail BIOS dumps are not redistributed and no C
derived from one may be committed. Releases bundle the MIT-licensed OpenBIOS; see the BIOS section for its current limitation.

`generated/` (the recompiled game C) **is committed**: releases ship the
compiled game, built by CI from that tree. Regenerate and commit it whenever
seeds or the framework pin change.

Default app icon: `assets/psxrecomp.ico` (and `.png` / `.svg`) — Retro-themed controller mark from `psxrecomp/assets/`. Windows builds embed it via `APP_ICON`.

Optional box art under `launcher_assets/img/` may come from
[libretro-thumbnails](https://github.com/libretro-thumbnails/libretro-thumbnails)
(`Named_Boxarts`); see `BOXART_SOURCE.txt` when present.

## Features (v0.4.0)

- **Native 16:9 widescreen** (mod feature *16:9 Widescreen*, off by default):
  the 3D view is rendered natively wide rather than stretched, HUD elements are
  anchored to the left and right screen edges, the pause menu stays centred,
  and the Level Select Etch-A-Sketch frame is extended to the full width.
  The original 4:3 rendering is unchanged when the feature is off.
- **Skip Startup Intro** (mod feature, off by default): skips the legal and
  logo screens and goes straight to the Press Start screen.

Enable features in the launcher's mod/enhancement settings.

## Installation

1. Download `toystory2-<version>-<platform>.zip` from the Releases page and
   unpack it.
2. Start `ToyStory2_Recompiled`. On first run the setup wizard asks for your
   game disc image (`.cue`) and, optionally, a BIOS. You must supply your own
   legally obtained disc of *Toy Story 2: Buzz Lightyear to the Rescue!* (USA,
   SLUS-00893). No disc image or retail BIOS is included.
3. Enable *16:9 Widescreen* in the enhancements menu if wanted.

## BIOS

Releases bundle **OpenBIOS** (MIT licence). Retail BIOS images are never
distributed.

> **OpenBIOS compatibility (v0.4.0):** the game calls the BIOS `free()`
> (A0:34h) on a pointer inside its own memory pool (not a BIOS `malloc`
> block). Retail SCPH-1001 `free()` only sets bit 0 of the word before the
> pointer; OpenBIOS instead walks a free list through game-overwritten
> memory and never returns, which showed as a black screen after the Andy's
> House movie. The bundled, always-on `bios-free-compat` mod reproduces the
> retail behaviour for this call. Manually tested on **OpenBIOS, macOS x64**:
> the first-run wizard, Andy's House gameplay after the movie, multiple
> levels and transitions, exiting/re-entering the level, savestates across a
> full restart, memory-card functionality, widescreen 16:9 and classic 4:3.
> A diagnostic run recorded 24 accepted compatibility-hook calls, zero
> pass-through calls, and only the four previously identified return
> addresses; every write set only bit 0 of the preceding word. This is
> targeted manual coverage, not full-game or cross-platform validation.
> Windows, Linux and macOS arm64 remain untested.

### Using a retail SCPH-1001 (workaround)

Release zips bundle a toolchain that builds a BIOS backend from **your own**
SCPH-1001 dump (CRC32 `37157331`, 512 KiB, v2.2) on first use. Tested with
the macOS x64 package only; other platforms are untested. A C compiler must
be available on `PATH` on macOS and Linux (Xcode Command Line Tools, or
`gcc`/`clang`); Windows is untested.

1. Unzip the release and run `ToyStory2_Recompiled`.
2. In the launcher, select your disc image, then open the **BIOS** setting
   and pick your `SCPH1001.BIN`. The file is only referenced, never copied.
3. The launcher shows "Prepare it once for this build (about a minute)".
   Click **Prepare** and wait for it to finish.
4. Press **Play**. The backend is cached under `cache/bios/` beside the
   executable and reused on later launches.

Other retail images (SCPH-5500/5501/5552/101) have build profiles but are
untested. The generated BIOS code is a local cache; never redistribute it.

## Recommended settings

- **CD speed 4x or lower.** At 8x, *Exit Level* leaves the screen black.
- Widescreen needs the OpenGL renderer (the default).

## Known limitations

- OpenBIOS compatibility was manually verified across multiple levels on
  macOS x64 (see above); not every level or platform has been tested.
- Native widescreen is verified in Andy's House and the Level Select screen;
  other levels have not been fully tested and may show culling gaps or
  misplaced HUD elements at the screen edges.
- The Level Select frame side fillers are reconstructed from the original frame
  textures; small differences at the extreme edges are possible.

## Savestates

- Savestates are tied to the exact build: they record a hash of the
  recompiled game code and a memory-layout value, and a state that does not
  match is rejected. **Savestates from earlier builds (v0.3.x and development
  builds) may therefore not load in v0.4.0**; we have not tested them
  one by one.
- With *16:9 Widescreen* enabled, the mod reserves a small amount of GPU-DMA
  memory at start-up, which changes the memory layout. States saved with
  widescreen on and off may be incompatible with each other (not tested).
  Within one setting, states saved before and after visiting Level Select
  load correctly (tested, SCPH-1001 build).
- Use the in-game memory card for long-term progress; savestates are for
  convenience and may not survive future updates.

## Building from source

The pinned `psxrecomp` submodule (`7cddab82`) is used with **one versioned
patch**, `patches/psxrecomp-screen-x-interp.patch`, which makes the dirty-RAM
interpreter honour `[widescreen.cull] screen_x_sites`. Apply it after checkout
and before configuring:

```bash
git submodule update --init --recursive
bash tools/apply_psxrecomp_patch.sh   # idempotent; verifies the pin and the patch
```

CI does this automatically. Do not bump `psxrecomp` without regenerating the
patch (the script refuses to run on a different base commit).

## Quick start (dev)

```bash
git submodule update --init --recursive
./psxrecomp/tools/ci/build_emitters.sh
python3 psxrecomp/psxrecomp_cli.py generate \
  --config game.toml --project-root . --disc disc/<your>.cue
git add generated && git commit -m "Regenerate game C"
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target psx-runtime
```

Releases: tag `vX.Y.Z` (or run the *Release builds* workflow). CI builds the
committed `generated/` C on Linux, Windows and macOS and attaches
`toystory2-<version>-<platform>.zip`, the compiled game. Locally:
`scripts/package_release.sh build-release linux-x64`.

## Symbols

Progressive map: `symbols.toml` → `python3 tools/sync_symbols.py` →
`psx_symbols.h` (`PSX_FN_*`). See `psxrecomp/docs/SYMBOLS.md`.

## Framework pins

Submodule gitlinks (`psxrecomp`, optional `recomp-ui`, nested `recomp-net`)
are authoritative. `framework_pins.txt` is an optional scaffold snapshot;
release CI logs SHAs with `record_pins.sh` but builds whatever the gitlinks
resolve to. Bump submodules deliberately — do not float on `main`/`master`
in release CI.

<!-- retcomm-readme-raid -->
---

<p align="center">
  <sub><b>R.A.I.D. — Retro AI Development</b> · a Discord for AI-assisted retro reverse-engineering, decomp &amp; recomp</sub>
</p>

<p align="center">
  <a href="https://discord.gg/Ad9BwSzctP"><img src=".github/raid-discord.png" alt="Join the Retro AI Development (R.A.I.D.) Discord" width="200"></a>
</p>
<!-- /retcomm-readme-raid -->
