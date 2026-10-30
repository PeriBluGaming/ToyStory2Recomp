# Enhancements

[← Back to README](../README.md)

ToyStory2Recomp bundles four mod packages. Three are optional enhancements
that are **off by default**; the fourth is an always-on compatibility fix.
Enable the optional ones in the launcher's mod/enhancement settings.

| Enhancement | Default | Available in |
|---|---|---|
| [16:9 Widescreen](#169-widescreen) | Off | v0.4.0 and later |
| [Skip Startup Intro](#skip-startup-intro) | Off | v0.4.0 and later |
| [Modern Camera](#modern-camera) | Off | v0.5.0 and later |
| [BIOS free() Compatibility](#bios-free-compatibility) | Always on (hidden) | v0.4.0 and later |

The launcher also lists general enhancements that come with the psxrecomp
framework itself, such as *CD Speed*, *Fast Loading*, *PGXP Precision* or
*HD Texture Packs*. They are not specific to this game and are documented by
[psxrecomp](https://github.com/mstan/psxrecomp).

## 16:9 Widescreen

Package `toystory2.enhancement.widescreen` (*Widescreen 16:9*), feature
*16:9 Widescreen*.

- The 3D view is rendered **natively wide** instead of being stretched.
- HUD elements are anchored to the left and right screen edges; the pause menu
  stays centred.
- The Level Select Etch-A-Sketch frame is extended to the full width. Its side
  fillers are reconstructed from the original frame textures.
- When the feature is off, the original 4:3 rendering is unchanged.

Requirements and caveats:

- Needs the OpenGL renderer (the default).
- Use CD speed 4x or lower.
- Verified in Andy's House and on the Level Select screen; other levels have
  not been fully tested. See [Known limitations](known-limitations.md).
- Enabling it reserves a small amount of GPU-DMA memory at start-up, which
  changes the memory layout and can make savestates incompatible between the
  two settings. See [Savestates](known-limitations.md#savestates).

## Skip Startup Intro

Package `toystory2.enhancement.skip-startup-intro`, feature
*Skip Startup Intro*.

Skips the legal screens, the ESRB screen and the startup movies, and goes
straight to the game's original *Press Start* title screen. It uses the
game's own "intro already seen" path, the same one the game takes when the
player skips the legal screens.

For shorter loading you can additionally enable psxrecomp's separate
*CD Speed* enhancement (keep it at 4x or lower, see above).

## Modern Camera

Package `toystory2.enhancement.modern-camera`, feature *Modern Camera*.

> **Availability:** Modern Camera is included starting with
> [v0.5.0](https://github.com/PeriBluGaming/ToyStory2Recomp/releases/tag/v0.5.0).
> It is **not** part of v0.4.0 or earlier releases.

The right analog stick controls the follow camera around Buzz, like in a
modern 3D platformer:

- **Left/right** orbits the camera around Buzz.
- **Up/down** raises or lowers it. By default, stick up looks up (the camera
  moves down).
- The chosen angle is **kept** until you press **L2** or **R2**, which hand the
  camera back to the Camera Mode selected in the pause menu (*Active* or
  *Passive*). L2/R2 keep their original meaning.
- Clicking the right stick (**R3**) swings the camera back behind Buzz with a
  smooth 0.4-second ease-out. Moving the right stick cancels the recenter.
  **Select + R3** remains the Rewind shortcut and never recenters.
- The game's own camera collision, obstacle avoidance and look-at stay in
  charge. Movement stays camera-relative, as in the original.
- Scripted cameras, transitions and the attract demo are not affected.
- After loading a savestate the camera starts with the original behaviour
  until you use the stick or R3 again.

### Options

| Option | Default | Range |
|---|---|---|
| Turn speed (degrees per second) | 120 | 30–360, step 10 |
| Tilt speed (degrees per second) | 60 | 10–180, step 10 |
| Right stick deadzone (%) | 15 | 0–50; applied on top of the controller deadzone |
| Invert horizontal | Off | Swaps the turn direction |
| Invert vertical | Off | Off: stick up looks up. On: stick up moves the camera up |
| R3 Camera Recenter | On | Turn off to disable R3 recentering |

## BIOS free() Compatibility

Package `toystory2.enhancement.bios-free-compat`. This is **not** a visual or
gameplay option: it is enabled by default and hidden from the enhancement
list.

It lets the bundled open-source OpenBIOS run Toy Story 2. Without it, the game
shows a black screen after the Andy's House movie on OpenBIOS. The fix
reproduces the retail BIOS behaviour for one specific `free()` call and runs
on any BIOS. [BIOS](bios.md#openbios-compatibility) explains the technical
background and its test coverage.
