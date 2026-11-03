# DKC1Recomp v0.0.13 - Dixie Kong update

The Windows package adds optional **Mods > Dixie Kong Country**, fixes the
reported map sprite corruption, and retains v0.0.12's in-game save persistence.
It contains `DKC1Recomp.exe`, `dkc1_dixie_desktop.exe`, `SDL2.dll`, licenses,
instructions and a hash manifest. Keep both executables beside the DLL. The
same verified clean USA v1.0 ROM supplies stock and Dixie; no ROM or private
save is bundled. Dixie applies its embedded IPS patch in memory.

Switching the mod restarts the game. Use 4:3 for the tested Dixie presentation.
The earlier Baby Kong menu option is replaced by Dixie. Saved graphics from a
corrupt older state can persist until a normal scene reload; preserve original
states before recovery. See [map diagnosis and refresh](DIXIE_MAP_FIX_2026-09-14.md)
and [cartridge save persistence](INGAME_SAVES.md).

**Game > Controller rumble (enemy stomps)** exposes the existing haptics
setting and persists it across launches. **Game > Test controller rumble**
sends a short pulse to a supported controller. Dixie uses a different stomp
rebound from DK; its detector now recognizes that verified value. Rumble is
enabled by default. Physical motor output remains unverified here.

## Validation

The packaged `BUILDINFO.json` records final source and executable identities.
The source incorporates the upstream v0.0.12 save fix. Engine pin:
`de94587464524ebac8dcd62e14618bf0d3953644` on `dkc1-dixie-v0.0.13`.

- Both Windows SDL targets and all four stock/Dixie direct host/tool build
  scripts pass. The public suite passes 247 tests with 10 optional skips,
  including native mapper and stock/Dixie
  stomp-detector checks. Windows CTest includes graphics, music, preferences,
  and both variants' actual worker-to-SDL virtual-controller rumble tests.
  Haptics preference persistence, checkmarks and disabled-test state are checked.
- Both executables from the extracted candidate ZIP pass their actual GPU
  graphics tests. The packaged Dixie app enters Ropey Rampage in three
  600-frame replays from the refreshed immutable map. Each matches the
  headless machine-state oracle, excluding only the existing verifier's host
  audio consumption cursor; guest graphics/game state is compared directly.
- The rebuilt headless Jungle entry and quickload legs each pass three
  byte-identical repeats. The exact-user-state map refresh contract passes
  three checkpoints over three byte-identical repeats, including image/audio
  hashes and zero integrity violations.
- The fresh-boot Dixie stomp/landing contract passes four checkpoints over
  three identical repeats. The actual C detector recognizes frame 149 of the
  immutable-state trace, while normal jumps and damage remain rejected.
  See [haptics diagnosis and tests](DIXIE_HAPTICS.md).
- Three packaged stock Candy-save/cold-restart runs preserve exact disk SRAM
  and match their cartridge oracles. This reuses the documented controlled
  Candy destination fixture; it is not a controller-only campaign playthrough.
  Audio output-queue exclusions remain those documented by the save verifier.
- The actual packaged SDL window was inspected displaying clean Dixie/Ropey
  gameplay. Private evidence, build logs, reports and captures are under
  `build/release-v0.0.13/`; final package checks are under `verification-final/`.
- The final ZIP is recreated from the same tested executable bytes and public
  documentation. Its allowlist, CRCs, per-file hashes and SHA-256 sidecar are
  checked before publication. Publication metadata records uploaded digests.

## Mac asset

The latest available Mac binary is **v0.0.9 for Apple Silicon**, carried
forward unchanged from the v0.0.11 release as
`DKC1Recomp-v0.0.9-macOS-arm64.zip`. Its SHA-256 is
`f83f4408075a4d892cfdd784817aba558e562d20cb7040727a1cb3ddce8a96ba`;
the original checksum file is retained too. It is not rebuilt on Windows and
contains neither Dixie nor the v0.0.12 in-game save fix. Existing release
assets are preserved.

This is targeted native character/map, package and save validation. Full-game
completion, all entrances/bosses/animal buddies, exhaustive widescreen,
physical-controller rumble and new macOS runtime coverage remain unverified.
No shared widescreen capability is promoted by this release.
