# Retro Launcher

[← Back to README](../README.md)

You can run ToyStory2Recomp **standalone**: download the release zip, point it
at your disc, and play. See [Installation](installation.md).

Alternatively, you can manage installs, updates, and disc/BIOS wiring with
**[Retro Launcher](https://github.com/RetroPortingToolKit/Retro-Launcher)**,
the Retro Compilation Manager hub for self-compiling recomps. Retro Launcher is
a separate project under the RetroPortingToolKit organization; it is optional
and not required to play.

- [Retro Launcher downloads](https://github.com/RetroPortingToolKit/Retro-Launcher/releases)
- [Full README and features](https://github.com/RetroPortingToolKit/Retro-Launcher#readme)

## What it does

- Checks for updates and installs the prebuilt release zips.
- Rebuilds with existing build data when possible, and shares the portable
  toolchain used by per-title launchers.
- Automates BIOS, disc image and save plumbing, so you do not have to repeat
  each game's first-run setup by hand.
- Queues builds for several titles and runs them in the background.

## Screenshots

Screenshots from the Retro Launcher project:

<p align="center">
  <img src="https://raw.githubusercontent.com/RetroPortingToolKit/Retro-Launcher/main/docs/screenshots/hub-and-game-launcher.png" alt="Retro hub with a background build, next to a title’s recomp-ui launcher" width="720">
</p>

<p align="center">
  <img src="https://raw.githubusercontent.com/RetroPortingToolKit/Retro-Launcher/main/docs/screenshots/queue-and-background-build.png" alt="Background cmake build with titles queued" width="720">
</p>

## Setup

Setup and usage are documented in the
[Retro Launcher README](https://github.com/RetroPortingToolKit/Retro-Launcher#readme).
You still need your own disc image of the USA release (SLUS-00893); Retro
Launcher does not provide game data or a retail BIOS.

## Note for maintainers

The README's short *Optional: Retro Launcher* section replaces the
`retcomm-readme-launcher` block that psxrecomp's Project Studio
(`patch_readme_metrics`) generates. That tool only replaces a block between
`<!-- retcomm-readme-launcher -->` markers or under a heading of exactly
`## Retro Launcher`, and only appends a new one when the README has no link to
`RetroPortingToolKit/Retro-Launcher`. The short section has none of the
markers, uses a different heading and keeps that link, so re-running the tool
leaves it unchanged. To restore the generated block, re-add the markers.
