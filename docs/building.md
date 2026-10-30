# Building from source

[← Back to README](../README.md)

ToyStory2Recomp is a game project in psxrecomp's New Project Layout. The full
flow is described in psxrecomp's
[Game project setup guide](https://github.com/mstan/psxrecomp/blob/7cddab8237e1f73b7067b8401af602d90a96b80d/docs/GAME_PROJECT_SETUP.md)
and its
[build documentation](https://github.com/mstan/psxrecomp/blob/7cddab8237e1f73b7067b8401af602d90a96b80d/docs/BUILDING.md).

## Requirements

- Git, CMake 3.20 or later, Ninja, Python 3 and a C/C++ compiler.
- Your own disc image of the USA release (SLUS-00893) if you want to
  regenerate the game C. Building the committed `generated/` tree does not
  need the disc.

## psxrecomp pin and patches

The pinned `psxrecomp` submodule (`7cddab82`) is used with **two versioned
patches**, applied in order:

1. `patches/psxrecomp-screen-x-interp.patch` makes the dirty-RAM interpreter
   honour `[widescreen.cull] screen_x_sites`.
2. `patches/psxrecomp-windows-toolchain.patch` makes Windows runtime
   overlay/BIOS compilation require a native (non-Cygwin/MSYS) compiler and
   rejects incompatible cached DLLs before they are loaded.

Apply them after checkout and before configuring:

```bash
git submodule update --init --recursive
bash tools/apply_psxrecomp_patch.sh   # idempotent; verifies the pin and the patches
```

The patches are applied to the submodule's working tree only; the pinned
gitlink stays on the upstream commit. `bash tools/apply_psxrecomp_patch.sh
--check` reports the state without changing anything.

CI does this automatically. Do not bump `psxrecomp` without regenerating the
patches: the script refuses to run on a different base commit.

## Build

```bash
git submodule update --init --recursive
bash tools/apply_psxrecomp_patch.sh
./psxrecomp/tools/ci/build_emitters.sh
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target psx-runtime
```

## Regenerating the game C

`generated/` (the recompiled game C) **is committed**: releases ship the
compiled game, built by CI from that tree. Regenerate and commit it whenever
the seeds or the framework pin change:

```bash
python3 psxrecomp/psxrecomp_cli.py generate \
  --config game.toml --project-root . --disc disc/<your>.cue
git add generated && git commit -m "Regenerate game C"
```

Committing `generated/` does not license it: it is derived from the original
game's code. See [NOTICE.md](../NOTICE.md).

## Symbols

Progressive symbol map: `symbols.toml` → `python3 tools/sync_symbols.py` →
`psx_symbols.h` (`PSX_FN_*`). See psxrecomp's
[symbols documentation](https://github.com/mstan/psxrecomp/blob/7cddab8237e1f73b7067b8401af602d90a96b80d/docs/SYMBOLS.md).

## Mods

The project's mods live in `mods/src/` (C) and
`mods/preloaded/packages/` (manifests). The build stages the catalog next to
the executable as `mods/bundled/`; that tree is build output and is wiped on
every build. Player-installed `.psxmod` archives live in `mods/installed/` and
are managed by the launcher's Mods manager. See psxrecomp's
[mod package documentation](https://github.com/mstan/psxrecomp/blob/7cddab8237e1f73b7067b8401af602d90a96b80d/docs/MOD_PACKAGES.md).

## Framework pins

Submodule gitlinks (`psxrecomp`, optional `recomp-ui`, nested `recomp-net`)
are authoritative. `framework_pins.txt` is an optional scaffold snapshot;
release CI logs SHAs with `record_pins.sh` but builds whatever the gitlinks
resolve to. Bump submodules deliberately; do not float on `main`/`master` in
release CI.

## Releases

Push a tag `vX.Y.Z`, or run the *Release builds* workflow manually (its
`publish` input defaults to `false`, which builds and verifies without
creating a GitHub release). CI builds the committed `generated/` C on Linux,
Windows and macOS (Intel and Apple Silicon) and attaches
`toystory2-<version>-<platform>.zip`, the compiled game.

When run manually, the workflow takes the version from its `version` input
(`X.Y.Z` or `vX.Y.Z`). If that is empty, it bumps the latest `vX.Y.Z` tag by
the `bump` input (`patch` by default, or `minor`/`major`); the `VERSION` file
is only a fallback when no tag exists. A minor release after v0.5.0, for
example, needs `version` set to `0.6.0`, or `bump` set to `minor`. The
workflow refuses
a version whose tag already exists.

CI also verifies each zip: the expected game files are present, no build kit,
disc, BIOS dump or developer mod state is included, and the license and
notice files (`LICENSE`, `NOTICE.md`, `licenses/`, `bios/OpenBIOS.LICENSE`,
the font and image notices) are present and correct.

Package locally:

```bash
scripts/package_release.sh build-release linux-x64
```

## Repository rules

- Disc images under `disc/` are gitignored and must never be committed.
- Retail BIOS dumps are not redistributed, and no C derived from one may be
  committed.
- Default app icon: `assets/psxrecomp.ico` (and `.png` / `.svg`), the
  Retro-themed controller mark from psxrecomp's `assets/`. Windows builds
  embed it via `APP_ICON`.
- Optional box art under `launcher_assets/img/` may come from
  [libretro-thumbnails](https://github.com/libretro-thumbnails/libretro-thumbnails)
  (`Named_Boxarts`); see `BOXART_SOURCE.txt` when present. Box art is
  third-party game artwork and is not covered by this project's license.
