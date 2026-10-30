# Installation

[← Back to README](../README.md)

## What you need

- A disc image of **Toy Story 2: Buzz Lightyear to the Rescue!**, USA release
  (serial **SLUS-00893**), as a `.cue` file with its tracks. You must supply
  your own legally obtained copy; no disc image is included.
- Optionally, your own retail PlayStation BIOS dump. It is **not** required:
  releases bundle the open-source OpenBIOS. See [BIOS](bios.md).

## Supported platforms

Every release provides one zip per platform, named
`toystory2-<version>-<platform>.zip`. The latest release is
**[v0.5.0](https://github.com/PeriBluGaming/ToyStory2Recomp/releases/tag/v0.5.0)**.

| Platform | Release file | v0.4.0 | v0.5.0 (latest) |
|---|---|---|---|
| Windows x64 | `toystory2-<version>-windows-x64.zip` | Runtime-tested | CI verified; runtime testing not confirmed |
| Linux x64 | `toystory2-<version>-linux-x64.zip` | Runtime-tested (Steam Deck) | CI verified; runtime testing not confirmed |
| macOS Intel | `toystory2-<version>-macos-x64.zip` | Runtime-tested | CI verified; **manually tested** |
| macOS Apple Silicon | `toystory2-<version>-macos-arm64.zip` | Built by CI, **not runtime-tested** | CI verified; runtime testing not confirmed |

Two kinds of testing are listed:

- **CI verified:** the GitHub Actions release workflow built the game,
  packaged the release zip and checked its contents automatically. It does not
  play the game.
- **Manually tested:** the release build was played on real hardware. For
  v0.5.0, the macOS x64 build artifact was tested on a real Intel Mac: the game
  launched, and all included mods worked, including the new Modern Camera.

The detailed OpenBIOS coverage described in [BIOS](bios.md) was performed on
macOS x64 with v0.4.0.

## Install and first run

1. Download the zip for your platform from the
   [latest release](https://github.com/PeriBluGaming/ToyStory2Recomp/releases/latest)
   and unpack it.
2. Start `ToyStory2_Recompiled`.
3. On first run the setup wizard asks for your game disc image (`.cue`) and,
   optionally, a BIOS. The disc is only referenced, never copied.
4. Optionally enable enhancements such as *16:9 Widescreen* in the launcher's
   mod/enhancement settings. See [Enhancements](enhancements.md).

Saves land in `saves/`; settings are stored beside the executable.

Instead of installing manually, you can let
[Retro Launcher](retro-launcher.md) download, install and update releases.

## Recommended settings

- **CD speed 4x or lower.** At 8x, *Exit Level* leaves the screen black.
- **16:9 Widescreen needs the OpenGL renderer**, which is the default.

## What a release contains

The zip holds the compiled game, the bundled OpenBIOS (`bios/`, MIT licensed),
the mod catalog (`mods/bundled/`), an overlay toolchain that compiles code the
game streams from your disc on your machine (`overlay_toolchain/`), and the
license and notice files (`LICENSE`, `NOTICE.md`, `licenses/`). It never
contains a disc image, a retail BIOS, or generated source code.

`LICENSE` and `NOTICE.md` ship at the zip root starting with v0.5.0; the
v0.4.0 zips do not contain them.
