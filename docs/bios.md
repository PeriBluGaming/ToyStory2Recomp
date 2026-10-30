# BIOS

[← Back to README](../README.md)

Releases bundle **OpenBIOS**, the free, from-scratch PlayStation BIOS from the
[PCSX-Redux](https://github.com/grumpycoders/pcsx-redux) project (MIT
license). You do not need a retail BIOS to play. Retail BIOS images are never
distributed by this project.

## OpenBIOS compatibility

The game calls the BIOS `free()` (A0:34h) on a pointer inside its own memory
pool, not on a BIOS `malloc` block. Retail SCPH-1001 `free()` only sets bit 0
of the word before the pointer. OpenBIOS instead walks a free list through
memory the game has overwritten and never returns, which showed as a black
screen after the Andy's House movie.

The bundled, always-on **BIOS free() Compatibility** mod
(`toystory2.enhancement.bios-free-compat`) reproduces the retail behaviour for
this call. Anything that is not an aligned main-RAM pointer is passed through
to the original BIOS code.

**Test coverage (v0.4.0).** Manually tested on **OpenBIOS, macOS x64**: the
first-run wizard, Andy's House gameplay after the movie, multiple levels and
transitions, exiting and re-entering a level, savestates across a full
restart, memory-card functionality, widescreen 16:9 and classic 4:3. A
diagnostic run recorded 24 accepted compatibility-hook calls, zero
pass-through calls, and only the four previously identified return addresses;
every write set only bit 0 of the preceding word.

This is targeted manual coverage, not full-game or cross-platform validation.

## Using a retail SCPH-1001 (workaround)

Release zips bundle a toolchain that builds a BIOS backend from **your own**
SCPH-1001 dump on first use:

- Required dump: SCPH-1001, CRC32 `37157331`, 512 KiB, v2.2.
- Tested with the **macOS x64** package only; other platforms are untested for
  this workaround.
- On macOS and Linux a C compiler must be available on `PATH` (Xcode Command
  Line Tools, or `gcc`/`clang`).
- On Windows the bundled toolchain selects a native MinGW-w64 GCC and rejects
  Cygwin/MSYS compilers, but preparing a retail BIOS backend on Windows has
  not been tested.

Steps:

1. Unzip the release and run `ToyStory2_Recompiled`.
2. In the launcher, select your disc image, then open the **BIOS** setting and
   pick your `SCPH1001.BIN`. The file is only referenced, never copied.
3. The launcher shows "Prepare it once for this build (about a minute)". Click
   **Prepare** and wait for it to finish.
4. Press **Play**. The backend is cached under `cache/bios/` beside the
   executable and reused on later launches.

Other retail images (SCPH-5500, SCPH-5501, SCPH-5552, SCPH-101) have build
profiles but are untested.

> The generated BIOS code is a local cache built from your own dump. Never
> redistribute it, and never commit a BIOS dump or code derived from one to
> this repository.

## Related

- [Installation](installation.md)
- [Known limitations](known-limitations.md)
- psxrecomp's
  [BIOS selection documentation](https://github.com/mstan/psxrecomp/blob/7cddab8237e1f73b7067b8401af602d90a96b80d/docs/BIOS_SELECTION.md)
