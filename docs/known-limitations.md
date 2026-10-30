# Known limitations

[← Back to README](../README.md)

## General

- **CD speed:** use 4x or lower. At 8x, *Exit Level* leaves the screen black.
- **OpenBIOS:** compatibility was manually verified across multiple levels on
  macOS x64 (see [BIOS](bios.md#openbios-compatibility)); not every level or
  platform has been tested.
- **macOS Apple Silicon (ARM64):** built by CI, but **not runtime-tested**.
- **Retail BIOS workaround:** tested with the macOS x64 package only. See
  [BIOS](bios.md#using-a-retail-scph-1001-workaround).

## 16:9 Widescreen

- Native widescreen is verified in Andy's House and on the Level Select
  screen. Other levels have not been fully tested and may show culling gaps
  or misplaced HUD elements at the screen edges.
- The Level Select frame side fillers are reconstructed from the original
  frame textures; small differences at the extreme edges are possible.
- Widescreen needs the OpenGL renderer (the default).

## Savestates

- Savestates are tied to the exact build: they record a hash of the
  recompiled game code and a memory-layout value, and a state that does not
  match is rejected. **Savestates from earlier builds (v0.3.x and development
  builds) may therefore not load in v0.4.0**; they have not been tested one by
  one.
- With *16:9 Widescreen* enabled, the mod reserves a small amount of GPU-DMA
  memory at start-up, which changes the memory layout. States saved with
  widescreen on and off may be incompatible with each other (not tested).
- Within one setting, states saved before and after visiting Level Select load
  correctly (tested with an SCPH-1001 build).
- Use the in-game memory card for long-term progress. Savestates are for
  convenience and may not survive future updates.

## Modern Camera

- Not included in the published v0.4.0 release; currently only available when
  [building from source](building.md).
- The camera's chosen angle is not stored in savestates: after loading a
  state, the original camera behaviour applies until the stick or R3 is used
  again.
