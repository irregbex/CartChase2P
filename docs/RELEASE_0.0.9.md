# DKC1Recomp v0.0.9 — Mac graphics, controls, and aquatic fixes

This release brings the newer projects' graphics and host controls to the
native Mac app and includes the aquatic presentation work from the September
6 playtests.

- **Graphics:** Reconstruct with five detail modes and adjustable edge strength,
  softness and shading; CRT television presets with scanlines, phosphor masks,
  glow, halation and curvature; Raw/CRT/Composite/Trinitron color profiles.
- **Escape menu:** native Game, Graphics, Settings, Controls, Assist, Mods and
  Credits tabs, with five save slots and immediate graphics previews.
- **Controls and Assist:** two-player keyboard/gamepad remapping and source
  selection, analog deadzones, optional rewind and 3× fast-forward.
- **Pacing and audio:** steadier refresh qualification and bounded audio drift
  correction while retaining fixed 60 Hz cartridge emulation and independent
  Metal presentation.
- **Aquatic widescreen fixes:** native-edge and water-scroll alignment, cache
  boundary flashing and bottom-row repairs, plus source-verified Coral Capers
  wall and shaft continuations. Enable **Escape → Settings → Aquatic widescreen
  fixes**, then restart. This experimental option is off by default because
  full entrance coverage remains incomplete.

The local validation suite passes 241 tests with one unavailable fixture
skipped. Aquatic checks include three-repeat saved-state and movement A/B,
raw plane/native viewport and guest-state comparisons, earlier 50-route
regressions, and actual Mac-window inspection. The latest shaft report covers
climbing above and returning through the reported gap at 16:10 and 16:9.
Four fresh entrances are available; 36 required anchors, including fresh
Coral entry, remain unavailable. These checks do not establish whole-game
widescreen correctness or resolve every compositor pacing variation.

The download is for **Apple Silicon, macOS 26 or newer**, includes SDL2 and
third-party notices, and is ad-hoc signed. It is not Apple-notarized. Supply
your own supported DKC1 USA v1.0 ROM; no ROM, extracted game assets, saves,
private diagnostics, or generated game source are distributed in the ZIP.
The optional Baby Kong mod still requires your own supported DKC3 ROM.

Source investigations and earlier validation counts remain historical in the
linked documents. This release records the user's approval to commit and
publish the accumulated project and required engine work.

Release preparation also verifies the saved aquatic option in a private Mac
bundle: a clean profile starts with feature mask 0, selecting On and restarting
produces mask 31, and the resulting raw frame/guest hashes match the accepted
playtest build. The engine sprite model passes. The engine test-suite shell
wrapper cannot run under the system Bash 3.2 because of its pre-existing empty
array/nounset incompatibility; the relevant PPU test was compiled and run with
the exact commands from that wrapper instead.
