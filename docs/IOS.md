# Native iPhone and iPad build

The UIKit frontend uses the shared statically compiled game/runtime. It renders
a native-pixel widescreen view with optional Metal upscaling, stereo
audio, touch controls and two GameController inputs. It targets iOS 16 or later
and uses the scene lifecycle required by iOS 27. No SDL or JIT is needed.

## Build and install

Install Xcode with the iOS SDK, CMake and Python:

```sh
bash build_ios.sh iphonesimulator
bash build_ios.sh iphoneos
# Use your Apple development team for a signed device build:
DKC1_IOS_TEAM=YOUR_TEAM_ID bash build_ios.sh iphoneos
```

Outputs: `build/ios-<sdk>/Release-<sdk>/DKC1Recomp.app` and
`build/ios-<sdk>/dkc1_port.xcodeproj`. Unsigned device builds need signing before
installation. Automatic signing uses the configured Xcode account.
`DEVELOPER_DIR` selects Xcode; the script finds Xcode.app/Xcode-beta.app when
only Command Line Tools are selected. `DKC1_BUILD_DIR` and `DKC1_BUILD_JOBS`
override the output directory and parallelism.

If private generated sources are missing, supply the supported ROM as argument
2 or `DKC1_ROM`; the existing generator is used. The app contains no ROM or
save files. ROM checksum verification remains enforced.

### GitHub release package

The v0.0.14 IPA is an Apple Distribution-signed sideload artifact. Recipients
must re-sign it for their own devices. Its private provisioning profile is
omitted; it is not a universal direct-install or App Store package. The local
device build retains its profile for development installation.

After committing and rebuilding the exact release source with diagnostics OFF:

```sh
DKC1_IOS_TEAM=YOUR_TEAM_ID bash build_ios.sh iphoneos
python3 scripts/package_ios.py \
  --app build/ios-iphoneos/Release-iphoneos/DKC1Recomp.app \
  --out build/release-v0.0.14/public --version 0.0.14 \
  --identity 'Apple Distribution: YOUR_IDENTITY'
```

The packager requires clean committed source and matching embedded build/version
identities. It copies only allowlisted app resources, signs a separate profile-free
copy, compares executable instructions before/after signing, verifies the
extracted IPA, and emits public build metadata plus a SHA-256 sidecar. The
original device build is preserved. See [v0.0.14 release notes](RELEASE_0.0.14.md).

## Interface and persistence

- A sliding D-pad supports diagonals and simultaneous action-button touches.
  B jumps, Y rolls/runs, A swaps Kongs. X, L, R, Select and Start are available.
- Hold Y and press B simultaneously to run and jump. Double-tap Y to toggle
  holding it without a finger on the button; a green ring and `HOLD` label
  indicate the latched state. Double-tap Y again to release. Menu, backgrounding
  and cancelled input clear the latch along with physical touches.
- One thumb can start on Y and roll onto B: Y stays pressed, B presses while
  the thumb overlaps it, and rolling back releases B for another jump.
- Opening a game automatically enters landscape. The render width follows the
  display aspect: 418×224 on iPhone 16 Pro Max, instead of stretching 256×224.
  The SNES 7:6 pixel aspect stays fixed. Symmetric even-width rounding clips
  less than one added outside column at each edge; all original game columns
  remain visible. Nearest sampling keeps the source pixels sharp.
- Translucent buttons and a sliding D-pad overlay the full game surface, with
  press feedback and optional haptics. Controls respect the notch and home
  gesture area; the game extends beneath those areas. The ROM welcome screen
  also supports portrait. Fixed cartridge screens retain their original
  proportions with black margins when the renderer cannot prove wider art.
- Menu pauses the game/audio and opens a native sheet with Continue, Save
  State, Load State, Upscaling and Touch feedback. It scrolls on compact landscape screens.
- Menu → Upscaling offers Original pixels (default), Sharp Bilinear, Bilinear
  and Reconstruct. Reconstruct includes five detail modes and Strength,
  Softness and Shading sliders. The paused frame previews changes immediately;
  Done returns to the pause menu. Settings persist across app launches.
- Files imports a verified `.sfc`/`.smc` atomically as `Documents/game.sfc`.
- Candy saves use the existing atomic SRAM writer at `Documents/saves/save.srm`.
  Quick saves use `Documents/quicksave.state` and temporary-file/rename
  replacement. Documents are available through Files/Finder for backup.
- Backgrounding, audio interruption and controller disconnect clear touch
  input and pause playback. Use Menu → Continue when resuming is necessary.

`cmake/ios.cmake` isolates UIKit from desktop frameworks and shell choosers.
Only the main thread calls the runtime. `ios_audio_ring.c` provides a bounded
atomic stereo queue; AVAudioSourceNode only consumes PCM and emits silence on
underrun. The audio callback never touches game, PPU or APU state.

The mobile host selects an even source width through `Dkc1VideoSetRenderWidth`,
bounded by the pinned PPU's existing 448-pixel capacity. Desktop presets remain
256/308/342 pixels. Shared calibration, streaming, activation and gameplay
policies were not edited, and experimental cartridge widening stays disabled.
Existing wide activation adapters use the selected margin, so mobile width is
not a claim of stock-versus-wide gameplay equivalence or full-game promotion.
CRT/color filters, MSU-1, rewind, remapping and Kong mods are not exposed.
No engine/submodule/generated source was edited.

### Metal upscaling

`runner/ios/ios_graphics.m` presents the completed framebuffer through a
`CAMetalLayer` at the display's native pixel scale. It reuses
`runner/macos_graphics.m` and `macos_graphics.metal` unchanged, with the shared
graphics defaults/clamping. Source rendering remains 418×224 on the tested
phones; the view still applies the fixed 7:6 SNES pixel aspect. The filter
operates on a copy of the completed image, never the guest framebuffer or
machine state. Selecting a filter while paused does not step the runtime.

The GPU queue permits two frames in flight and never overwrites an input
texture still being sampled. A skipped presentation retries the latest image,
including paused settings changes. Shader/device failure falls back to the
original nearest-pixel view. Touch controls remain UIKit overlays above the
Metal surface. `NSUserDefaults` stores only the five exposed filter values
under `IOSUpscalingV1`; game saves are separate.

Xcode compiles `.metal` resources instead of copying their source, so CMake
copies the unchanged shader as `ios_upscaling.txt`. The iOS adapter supplies
that resource explicitly to the existing renderer. The shared Objective-C
renderer retains its existing manual memory management; the UIKit host uses ARC.

Validation: `build/ios-validation/upscaling/` contains the landscape settings
screen (`menu.png`), all four full-screen filters (`mode-0.png` through
`mode-3.png`), and GPU output comparisons. The existing native Metal oracle
also runs inside the iOS 26.5 simulator: all 12 cases are byte-identical to
macOS for the preserved 418×224 Jungle frame (native transfer at 418×224;
upscaling at 2926×1344). The cases include
nearest, bilinear, all five Reconstruct modes, Sharp Bilinear and existing
CRT coverage; unchanged-frame caching and pixel/settings/viewport invalidation
pass with zero mismatches. `gpu-comparison.json` records SHA-256 hashes.
The scene, immutable root and ROM identities are recorded below. The
251-test suite passes (one skip), and simulator/device Release builds pass.
This is display-filter validation, not additional widescreen promotion or a
claim of sustained phone GPU performance. Physical menu interaction and filter
preference remain available for the user's playtest; automated UI captures
select filters through diagnostic overrides.

The final Reconstruct simulator run advanced the immutable Jungle entry for
15 seconds with no held inputs or GPU errors; `live-reconstruct.png` shows
the Kongs on the ground with the transparent controls. Cleanup restored the
paused immutable root (`final-cleanup.log`). The signed device update was
installed and launched after the user unlocked the phone; PID 26021 remained
present in `build/ios-upscaling-processes.json`. No debugger was attached.
Deployment evidence: `build/ios-upscaling-install.json` and
`build/ios-upscaling-launch-unlocked.json`. Signed executable SHA-256:
`8f74ed5c790f056d5ef749908a8cd7135300ee00d96d8efe2e364ffc44970db5`.
The phone was left running; the IPA and updated hashes are in
`build/ios-release/`.

API references: [UIKit scenes](https://developer.apple.com/documentation/uikit/uiscene),
[document picker](https://developer.apple.com/documentation/uikit/uidocumentpickerviewcontroller),
[controller profile](https://developer.apple.com/documentation/gamecontroller/gcextendedgamepad),
[CMake Apple toolchains](https://cmake.org/cmake/help/latest/manual/cmake-toolchains.7.html).

## Validation — September 14, 2026

- Xcode 27.0 / 27A5237l; arm64 simulator and signed arm64 device Release builds.
  Source base: `f4c82a2` plus the uncommitted iOS frontend.
- Clean ROM: 4,194,304 bytes; SHA-256
  `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15`.
- `python3 -m unittest discover -s tests -v`: 251 tests, one skipped. Includes
  audio stereo ordering, overflow, underrun, counter wrap and reset, plus
  existing SRAM and ROM verification, native pixel geometry, width capacity,
  grading black margins at mobile width, simultaneous run/jump, double-tap
  latch on/off, long press, drag and cancellation. `git diff --check` passes.
- Three fresh-boot `recipes/route_jungle.dks` runs in the iOS 26.5 simulator
  match three desktop runs at frame 8070: framebuffer, WRAM, VRAM, CGRAM,
  PPU OAM, OAM shadow and audio. The route enters Jungle and reaches camera
  `$0200`. Evidence: `build/ios-validation/jungle-replays/comparison.json` and
  adjacent per-run logs, checkpoints, raw memories, frames and snapshots.
- Final framebuffer SHA-256:
  `9e8c67590fe4b11ca5886f5dd4884e685ab195cea07d174f6902c952a7579ea5`;
  audio FNV-1a: `fc07ebba6af7974a`; final snapshot SHA-256:
  `7c69162c967db087ad5495b69c562f6c3022b62b4bf0b0fa7e4155240a89113c`.
- Installed/launched on iPhone 16 Pro Max, iOS 27.0 (24A5408d). The running
  intro and touch interface were inspected through QuickTime's live phone feed.
  Physical Bluetooth-controller handling and sustained whole-game mobile
  performance remain unvalidated. Simulator screenshots cover portrait and
  landscape presentation; this is not full-game acceptance.

### Landscape without stretching

Symptom: the initial landscape overlay resized the 256×224 surface to the
entire phone, distorting it on the first landscape frame. The corrected host
computes the source width from display geometry, allocates to the PPU capacity,
and rebinds the pitch before drawing after a layout change. Rendering and
presentation use the same width. No cartridge/ROM patch or crop of the native
image was used. The orientation request keeps gameplay in landscape.

Evidence under `build/ios-validation/native-wide/`:

- `existing-presets.json`: native 256 and desktop 342 exact-state frame,
  WRAM and VRAM remain byte-identical to the pre-change executable.
- `live-exact/{342,418,448}/native-center.json`: zero changed native pixels
  from the immutable frame-8070 Jungle state. Native WRAM/VRAM are identical.
  At the left level wall, the native image starts at `extra - bias = 0`;
  comparing the geometric center would be the wrong coordinate domain.
- `replays/comparison.json`: three fresh-boot iOS and three desktop 418×224
  runs match all seven frame/memory/audio fingerprints. Final framebuffer:
  `302def98c32aaaa115177fccf270c868d03a4a651f4056e73b3bf83b051fbf5a`.
- `landscape-final.png` and `ui-final.log`: the complete visible simulator
  surface, correct proportions and transparent controls, paused at the same
  immutable frame-8070 state. Diagnostic pausing now survives initial scene
  activation. The phone was installed/launched; its launch log is
  `build/ios-device-console-native-wide-relaunch.log`. A debugger check ended
  an earlier live run with signal 9; this was operator-induced, not evidence
  of a spontaneous gameplay crash. The user reported it was working before
  that interruption; the final app was reopened and left running.
- `fresh-stress-418/report.json`: four available controller-only map entrances
  (`003E`, `00A7`, `006D`, `0024`), three native/wide repeats each, 360 entry
  frames and 180 neutral frames. Repeats are deterministic; all four wide
  terrain/strict-trace grades pass with zero terrain misses or raw margins.
  Native versus wide machine-state differences remain investigations.
- `transition/report.json`: all 27 retained/cold comparisons across four
  transitions on the controller-entered `003E` route pass, with no first
  failure. The private capture harness only connects the existing headless
  loop to the existing flight recorder; the hashed bundle passes
  `verify_flight_bundle.py`. Build commands and source remain beside it.
- `capability-floor-418.log`: the full 40-entrance floor remains incomplete
  because 36 clean pre-entry anchors are unavailable locally. Bosses, the
  complete route matrix and sustained whole-game mobile play are not claimed
  as validated. No shared decoder or presentation-policy promotion was made.

The initial stress invocation inherited a 418-wide override in both twins and
used the old 43-column grader. Its `fresh-stress/` report is invalid evidence;
`fresh-stress-418/` replaces it with explicit native/wide widths and an
81-column grading extent. `fresh-stress-342/` provides the existing desktop
preset comparison. Original supplied/local save files were never overwritten.

`build/ios-release/BUILDINFO.json` records the final signed executable and IPA
hashes. The IPA contains no ROM, battery save or private snapshot.

### iOS 27 launch crash

The first device build trapped before frame 1 in
`___UIApplicationEvaluateRuntimeIssueForNoSceneLifecycleAdoption_block_invoke`.
The iOS 26.5 simulator did not enforce this requirement. The device report is
`build/ios-validation/device-crash.ips`; failing executable UUID:
`F085BC2E-CC92-3FB8-BCC8-4EC1A3609066`.

The host-only fix declares the single Game scene, supplies `DKCSceneDelegate`,
and creates the window with `initWithWindowScene:`. The corrected signed app
boots and stays running on the same phone. `build/ios-device-console-fixed.log`
records runtime startup; `build/ios-install.json` records deployment. No game
logic, ROM, state contents or timing was changed to repair the launch.

### Run/jump touch chords and Y hold

The original adapter inferred game input from UIKit's transient button
highlight. The touch update instead tracks each touch identity and button
lifetime, explicitly allows concurrent controls, and uses an independent
controller state. Lifting B leaves Y held; a drifting thumb remains held until
release. A short pair of Y taps toggles the latch after the second release,
with no gesture-recognizer delay to normal game input. Two simultaneous
fingers on Y, a long press or a drag cannot become a double tap.

`tests/test_ios_touch_button.c` exercises the actual input state implementation
with overlapping run/jump and repeated jumps, latch on/off, B double taps,
long presses, drags, multiple fingers and stale releases after cancellation.
`build/ios-touch-tests.log` records the full 251-test run; signed device and
simulator builds are in `build/ios-touch-*-build.log`. The visible hold indicator
is captured in `build/ios-validation/touch-hold-y.png` from the paused immutable
QA state. Physical multi-finger feel still requires playtesting on the phone.
This changes only the UIKit input adapter; guest controller masks and runtime
timing are unchanged.

### One-thumb roll from Y onto B

The two-finger fix did not cover a thumb that starts on Y and rolls toward B.
UIKit continues delivering that contact to Y; Y's move handler previously
only invalidated double-tap detection. It never contributed a B press.

Y now checks each owned contact against B's stable layout geometry on touch
begin/move. It includes the reported thumb radius, bounded to 8–24 points,
and keeps a five-point release band to prevent jitter at B's edge. Each
overlapping contact contributes a separate B hold, independent of fingers
that started directly on B. Moving away, lifting or cancellation removes
only that contact's contribution. Y remains held until lift, and rolling
cannot accidentally toggle the double-tap latch. Press animations cannot
move the input boundary because hit testing uses untransformed layout bounds.

The actual C input model is tested with a single-contact Y→B→Y→B sequence,
overlap before the thumb center enters B, independent direct B touches,
release jitter, extreme/missing radius estimates, multiple rolling contacts
and cancellation. `build/ios-roll-tests.log` records 251 tests with one skip;
`build/ios-roll-*-build.log` records both Release builds. The paused visual
probe in `build/ios-validation/roll-y-b.png` shows both pressed highlights;
it is a display probe, not a claim of physical thumb testing. Device install
and launch records are `build/ios-roll-install.json` and
`build/ios-roll-launch.json`. Runtime/video code and the saved game are unchanged.

## Simulator diagnostics

`DKC1_IOS_DIAGNOSTICS` is OFF for normal device builds. When ON it embeds the
existing headless main under a renamed entry point, not a second replay engine.

```sh
DKC1_IOS_DIAGNOSTICS=ON bash build_ios.sh iphonesimulator
# Install on an explicitly chosen, booted simulator, then:
DEVELOPER_DIR=/Applications/Xcode-beta.app/Contents/Developer \
python3 tools/verify_ios_simulator.py \
  --simulator SIMULATOR_UDID --rom '/path/to/Donkey Kong Country (USA).sfc' \
  --headless build/ios-host-oracle/dkc1_snesrecomp_headless \
  --out build/ios-validation/new-run
```

The tool runs three iOS and three desktop replays, writes `comparison.json`,
and fails if any of the seven fingerprints differ. It uses the existing route,
checkpoint and output variables. `--route`/`--frames` override the defaults.
`--render-width 418` selects mobile resolution (default 256). The existing
headless and layer-capture hosts accept `DKC1_RENDER_WIDTH=256..448`, even
values only, overriding their aspect preset; invalid widths fail before
rendering. This override is unset by default. The fresh-entry stress tool's
`--render-width 418` sizes only the wide twin and its grader; its native twin
is always 256 and its default remains 342.
It replaces the diagnostic app's imported ROM and terminates it between runs;
do not use an active play session. The selected simulator is its only target.

The app's diagnostic `--verify` entry runs asynchronously after UIKit startup
and writes `Documents/verification-result.txt`. Use the existing
`DKC1_SAVESTATE_INPUT` or input/wait-only `DKC1_STARTUP_SCRIPT` for interactive
QA. `DKC1_IOS_QA_PAUSED=1` renders the loaded state and pauses;
`DKC1_IOS_QA_LANDSCAPE=1` requests landscape; `DKC1_IOS_QA_MENU=1` opens the
actual pause sheet. All UI overrides are compiled out of device releases.
`DKC1_IOS_QA_HOLD_Y=1` with `QA_PAUSED` drives the same double-tap input state
into its held state for visual inspection without advancing the game.
`DKC1_IOS_QA_ROLL_Y_B=1` with `QA_PAUSED` displays an active Y touch and a
separate rolled B contribution. Both probes are compiled out of device builds.
Capture the complete displayed UI using `simctl io <UDID> screenshot <path>`
and retain its raw frame/state companion.
