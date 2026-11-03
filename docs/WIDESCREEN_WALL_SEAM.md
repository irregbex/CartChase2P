# Coral Capers left-margin wall seam — September 6, 2026

The supplied save shows a vertical join 11 source pixels from the left edge at 16:10. The local playtest app now continues the adjoining wall with its own authored pattern, confined to that offscreen strip. This is a narrow, default-off presentation capability, not a general repair of populated map art.

## Reproduction and source evidence

Private evidence is under `build/repros/water-seam-20260906/`. The screenshot and original quicksave were copied before any run; the diagnostic work never overwrote the normal slot. A later observed version of the slot was preserved separately as `inputs/quicksave-latest.state`. The original visible window and executable bundle are preserved there.

- Supported ROM SHA-256: `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15`.
- Supplied state SHA-256: `82d7d898aa8ebd0d5a971e538db900f10f20406a210afb1922a9d5acc6ca88c5`.
- Baseline executable SHA-256: `e4c8551cfe6bdf020cd9e4c47e3daed1dedcf803802d09342756238550108bad`.
- First observed frame: **34978**, mode `$0003`, level `$0061`, entrance `$00BF`, camera **943,10113**, 308×224 source, Glide policy with zero current bias. The cartridge is paused in the supplied state. This is the first observation, not a claim about when the defect began.
- Map source `$E9`, definition source `$D0`, both published bases zero. The native terrain decoder matches **224/224** calibration samples. Map records and the RainbowZ level catalog identify entrance `$BF` as **Coral Capers**; older records that called this tuple Croctopus Chase should not be used to name this reproduction.

At world X **928**, the rendered strip crosses from column 28 to column 29 of the original 32×32 metatile map. Column 28's upper cells belong to a neighboring corridor. They meet the repeating rock wall in column 29 with an abrupt art discontinuity. Both columns contain real ROM tiles: this is neither a stale cache nor a missed VRAM upload. The existing empty-cell wall continuation correctly does nothing. Cold rendering reproduces the same seam, and Flat/Raw pixels show it before Reconstruct.

The same wall continues down to rows **325–327**, where its west-side cells join correctly. Their three-row phase matches column 29 at rows **316–322**. `map-original.png`, `map-continued.png`, raw VRAM/CGRAM, isolated planes, and the private source-map probe record that relationship. Atlas lookup was attempted but the local disassembly instruction index is absent; no cartridge-routine semantics are inferred from its names.

## Narrow correction

`DKC1_WS_WALL_SEAMS=1` opts into a verified authored junction capability in `runner/dkc1_wall_seams.h`, used only by host prefill in `runner/dkc1_game.c`.

The capability requires the accepted vertical layout, exact map/definition banks and bases, and all **24 metatile cells** of the two-column, twelve-row junction—including the donor strip—to match. A mismatch fails closed to the existing output. The target must be column 28, rows 316–322, wholly beyond a native left edge in column 29. It reads the corresponding source cell from rows 325–327; it does not generate pixels, change the ROM, or substitute art inside the native viewport. Already-correct lower rows and other columns are untouched. No scene tuple grants broader permission to alter populated terrain.

The correction uses the wall's complete three-row pattern, preserving the vertical phase. Copying the nearest individual edge tile would repeat distinctive fragments; softening the join in the graphics shader would hide the source mismatch and affect unrelated pixels. Global adjacency replacement was rejected because populated offscreen cells can contain legitimate terrain. The explicit source capability is deliberately limited to this verified junction, pending broader evidence.

Trace feature bit **16** and `wall_seam_tiles` record activation. The flag is excluded from serialized guest state, is off when absent, and is independent of the four earlier aquatic switches. No initializer, streamer, activation, collision, gameplay bounds, engine, or generated-source changes were made in this fix.

## Validation and limits

- Exact-state A/B changes **2,105 composite pixels**, all within left source columns **0–10** at 16:10. The entire 256-pixel native center and right margin match exactly. WRAM, VRAM, CGRAM, and both OAM hashes are unchanged. At 16:9, 5,468 composite pixels change only in the left margin.
- Isolated BG1 changes only on the left; BG2, BG3, and OBJ are exact. Native 4:3 remains byte-identical across all five surfaces. Three independent candidate layer captures at each aspect are identical (`layer-comparison.json`). Layer capture advances one canonical frame, so these are frame 34979; the zero-frame A/B separately covers 34978.
- Stay, Down, Up, and Right routes pass at both **308×224 and 342×224**, with three independent candidate replays each and a disabled oracle at the same aspect. Every per-frame center, right margin, and guest-memory hash matches; final states, frames, and full traces repeat exactly. Down and Right leave the patch region, ending at cameras 880/10513 and 1344/10309 respectively. Complete input schedules and hashes are in `validation-summary.json`.
- Exact/cold state loads agree. The transition sentinel passes **34 samples over five boundaries** of the prior controller-only aquatic entry/traversal flight bundle, with no failure bundle. This cross-layout bundle covers Croctopus, not the new Coral Capers junction.
- The four available authentic entrance anchors run three repeats through 360-frame entry settle plus 360-frame continuation. They retain the pre-existing native/wide machine-divergence investigation status; there are zero hard failures. None exercises this Coral Capers source capability. The complete 40-entrance floor remains unavailable and must not be claimed as passed.
- Unit/model suite: **241 tests, no failures, one skip**. The new model rejects every changed source cell, missing reads, wrong orientation, wrong target/edge columns, native overlap, and rows outside the patch. Host/headless builds and whitespace checks pass.
- Actual non-headless before/after windows were inspected at the preserved save. The candidate uses the user's Reconstruct mode 3, strength 100, softness 86, shading 99; `live/corrected-window.jpg` shows the seamless join.

**Fresh-entry limitation:** a clean pre-entry anchor for Coral Capers was not available. Controller-only exit/death probes from the supplied state did not reach a map transition. Those probes are not labeled fresh-entry proof, and the four other entrance anchors do not substitute for it. The fix addresses the ROM-authored junction rather than rewriting serialized corruption, but a fresh Coral Capers entry remains an outstanding promotion gate. All source defaults stay off; the normal local app enables this narrowly guarded fix for the user's requested playtest.

## Commands and local use

```sh
cmake --build build/macos --target dkc1_snesrecomp_headless dkc1_macos --parallel
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests -v
git diff --check
# Retain the other four aquatic switches when comparing this playtest build.
DKC1_WS_WALL_SEAMS=1 DKC1_ASPECT=16:10 DKC1_WIDESCREEN=1 \
  DKC1_SAVESTATE_INPUT=/absolute/preserved/quicksave.state \
  DKC1_FRAME_PPM=/absolute/output/frame.ppm \
  build/macos/dkc1_snesrecomp_headless /absolute/verified-rom.sfc 0
```

`baseline-manifest.json`, per-run environment/input files, `layer-comparison.json`, `validation-summary.json`, and `final-manifest.json` record identities and commands. The normal app preserves all graphics/control preferences and the original save slot. Source remains uncommitted for user verification. Disabling only `DKC1_WS_WALL_SEAMS` restores the prior junction behavior.

The rebuilt normal bundle passed strict signature verification and was relaunched. Final live inspection showed the loaded quicksave in 16:10 composite, with the player already beyond the original junction; play was left undisturbed. `final-manifest.json` records the executable and both preserved save identities. The saved corrected-window capture is from the exact-state visible QA run.


## Right-facing junction follow-up — September 6, 2026

A later save exposes the **opposite face of the same authored junction**. The native camera is now left of the wall, so extending the west-facing pattern would be incorrect. Private evidence is in `build/repros/water-right-seam-20260906/`; both the supplied image and current slot were copied before testing.

- State SHA-256: `18eeac13727416f8fd442f931cd933a068ec8c858c56695f29a8cfa20c4b2ea0`.
- Baseline app executable SHA-256: `e9de05b7aa7c0df2de7f5e9ea57973e38ba18dd4ad8b1189f96258ed36fb5a73`.
- Candidate normal app executable SHA-256: `558ae6f03814df929ddb506df5afadf52327c57746159dfae369d40e0cf25863`.
- Same supported ROM hash as above; first observed frame **78551**, mode 3, level `$0061`, entrance `$00BF`, camera **671/10048**, Glide bias 0, 16:10 (308×224). Exact neutral zero-frame replay reproduces the supplied seam. Cold-load output is identical.
- The native image ends at world X926. The source boundary at X928 joins column 28 to columns 29–30. Rows 313–315 contain unauthored empty margin cells, producing the blue gap; rows 316–322 contain the neighboring passage's mismatched wall. The vertical decoder still matches 224/224 native samples. Raw VRAM/CGRAM, isolated BG1, and the actual window establish a presentation defect, with no evidence of guest corruption.

`Dkc1EastWallSeamSourceMatches` requires all 36 original cells of columns 28–30, rows 312–323, plus 18 unique donor cells. The donor triples at `(50,306/307/308/310/311)` and `(32,300)` are authored within the same Coral Capers map. Each donor's first cell must exactly equal the target's native edge cell, including flip bits. The two following cells supply its matching rock interior. This repairs both empty and populated side art without treating other populated cells as disposable. `map-junction.png`, `donor-wall.png`, and `layer-comparison.json` preserve the visual and byte evidence.

`Dkc1EastWallSeamDonor` accepts only offscreen columns 29–30, rows 313–322, with native right edge in column 28. Every native pixel remains owned by the original renderer. The previous west-facing capability remains independent and unchanged. The existing default-off `DKC1_WS_WALL_SEAMS` switch and trace count cover both directions; source defaults, cartridge state, streaming, activation, and collision are unchanged.

Validation on this extension:

- Exact 16:10 A/B changes **4,982 pixels**, confined to source X282–307 in the right margin. 16:9 changes **8,375** right-margin pixels. BG1 accounts for every changed pixel; BG2/BG3/OBJ are identical. The left margin and native center are byte-exact. Native 4:3 is unchanged on all five captured surfaces. Each candidate layer capture repeats identically three times.
- Stay, Down, Up, Right, and Left input routes pass at both 308×224 and 342×224, each with three identical candidate repeats against the preserved pre-extension executable. All per-frame center/left and guest-memory hashes match, including WRAM, VRAM, CGRAM, PPU OAM and WRAM OAM. Up reaches camera 671/9897; Left reaches 212/10048 and leaves the repair region. Down and Right remain against the floor/wall at 671/10048 and are not claimed as camera traversal. Exact schedules and final hashes are in `validate.py` and `validation-summary.json`.
- The original left-seam save's four routes at both aspects remain pixel-exact against the previous implementation on all regions and guest-memory hashes (`previous-seam-regression.json`).
- Transition sentinel: **34 samples, five boundaries, passed**. Four available clean entrances retain zero hard failures with their pre-existing investigation status. The full floor check still reports 36 missing entrances; none of these runs supplies a clean Coral Capers entry.
- Unit/model suite: **241 tests, zero failures, one skip**. The new checks reject each changed or unavailable target/donor cell and every unsupported row, column, native edge or output pointer. Host/headless builds and strict normal-app signature verification pass.
- The rebuilt **normal app** was reopened at this exact immutable save with the user's existing Reconstruct preferences. `candidate/normal-app-window.jpg` confirms the right wall and blue-gap correction in the actual window. The original normal quicksave remains unchanged. No scheduled controller input was installed, and the app was left at the user's save for playtesting.

This remains a local opt-in fix with exact-state and nearby-scrolling validation. A fresh Coral Capers entry is still an outstanding promotion gate. No commit or release was made. `manifest.json`, `final-manifest.json`, `gate-commands.json`, and the layer/route reports contain identities and repeatable commands.


## Remaining fine lines: original raster effect

The user's subsequent `remaining-lines.jpg` marks finer lines inside the native image, after the large margin seam and blue gap were corrected. This is distinct from the donor-junction repair. A private reference-renderer capture at the same immutable save is byte-identical to the current native output on composite, BG1, BG2, BG3 and OBJ. An independent ROM tile decode, using the actual per-scanline scroll registers, matches all **6,294 nonblack source pixels** checked around the marked wall (allowing only SNES color-expansion rounding).

The original underwater HDMA alternates BG1 scroll between 671/832 and 672/833. At this frame its transitions fall at output rows 7, 17, 38, 60, 71, 81, 102, 124, 135, 145, 166, 188, 199 and 209. Those discrete one-pixel steps can make hard texture joins visible under Reconstruct. This is not evidence of another native streaming failure, nor justification for replacing more map cells.

`raster-probe/analysis.json` and its raw scanline log retain the reference comparison. A **private preview only** holds terrain scroll steady during each render call, then restores live scroll before the next HDMA operation. Other layers retain their original animation, and all five guest-memory hashes match. The preview was rendered with the user's Reconstruct mode 3, strength 100, softness 86 and shading 99. It removes the discrete terrain ripple; it does not claim to remove every edge in the original texture. The normal app still retains the cartridge effect. The user declined this cosmetic change; preserve the original ripple. The preview remains private and is not part of the normal app.


## Missing ceiling corner above the right wall

The next save, frame **110246**, mode 3, level `$0061`, entrance `$00BF`, camera **671/9898**, exposes row 312 above the earlier right-wall range. The original partial corner cell `0007` is followed by two empty cells, so the prior range beginning at row 313 leaves a rectangular blue hole. This was present at the endpoint of the earlier upward route: native/guest integrity checks alone did not grade that missing authored-art continuation.

The right-facing capability now starts at row 312. It validates **39 target cells** in columns 28–30, rows 311–323, and **21 unique donor cells**. The new donor triple `(25,284) = 0007,4005,00D5` exists repeatedly in the same map, and its native edge is exact. The donor's upper edge `00CE` also matches the target's original cell above. The general partial-wall rule remains unchanged; only this verified corner is eligible. No renderer, shader, HDMA, guest state or original native-image behavior changes.

Private evidence: `build/repros/water-hole-20260906/`. State SHA-256 `2d7cb4a582e22d02fbb4ac90558e85f667b5b6b942c635164448531f1f749567`; baseline app `558ae6f03814df929ddb506df5afadf52327c57746159dfae369d40e0cf25863`; updated app `70fcad1330ac4bcfef4cea2e081dd8351d171cd1ae9b390c976c7f2a510ece98`. Cold state reproduction matches the baseline. The supplied and actual-window screenshots, raw memories, source-map corner/donor captures, input schedules and final identities are retained there.

- Exact/layer A/B changes **846 right-margin pixels at 16:10**, **1,407 at 16:9**; BG1 accounts for all changes. Native center, left margin, BG2/BG3/OBJ and all native 4:3 surfaces are exact. Three layer repeats agree.
- A direct occupancy assertion covers the hole interior: **0/550 → 550/550** BG1 pixels at 16:10 and **0/924 → 924/924** at 16:9. `hole-occupancy.json` records exact rectangles. This supplements the native-center checks that previously missed it.
- Stay/Down/Up/Right/Left routes pass three candidate repeats at both aspects, with exact per-frame native/left and WRAM/VRAM/CGRAM/PPU-OAM/WRAM-OAM hashes against the preserved previous executable.
- **241 unit/model tests**, zero failures, one skip; host/headless builds, strict signature verification and whitespace checks pass. The model checks every changed/missing source cell, including the new corner and donor, and rejects row 311 and unsupported native bounds.
- The transition sentinel passes **34 samples/five boundaries**. Four available clean entrances retain zero hard failures and their pre-existing investigation status. Fresh Coral Capers entry and the complete 40-entrance floor remain unverified; source defaults remain off.
- The rebuilt normal app was loaded at this immutable save and inspected with the user's Reconstruct settings. `candidate/normal-app-window.jpg` shows the closed hole. The normal slot is unchanged. Original terrain ripple is retained at the user's request. Changes remain uncommitted.

## Western alcove: outer strip and lower rectangular hole

The next supplied save is frame **541651**, mode 3, level `$0061`, entrance `$00BF`, camera **127/9797**. It is the western alcove represented by camera record 24 in the provisional camera inventory. Its source wall ends at column 12 on rows 299–310 and column 11 on rows 311–312. The unused cells to the right are empty. The adjacency option rejects the short lower void and cannot resolve every outer-wall continuation, exposing both the tall thin blue strip and the larger lower blue rectangle. Turning adjacency off fills these cells by repeating wall art, but does not provide the matching authored strips selected here. Cold loading reproduces the same defects, so no serialized-corruption repair is warranted.

The new `Dkc1CoveWallSourceMatches` capability validates all **64 cells** of columns 11–14, rows 298–313, including both guard rows. It also checks **26 donor cells** in eight strips elsewhere in the same E9/D0 source map; two overlap the target signature. Every strip begins with the exact retained wall cell, including flip bits. The original wall and each nonempty neighboring cell stay intact. `Dkc1CoveWallDonorCell` can replace only columns after that verified wall, through column 14, on rows 299–312, while the native right edge is at or left of the wall. The upper camera endpoint and the two-row lower corner have their own authored donor strips. This is a separate source-backed capability under the existing default-off `DKC1_WS_WALL_SEAMS` option. The general adjacency/void heuristics and previous seam capabilities are unchanged.

The **middle marked join is original terrain**, not another widened tile replacement. All **957** 8×8 entries intersecting the original viewport match an independent decode of the clean ROM's map and metatile definitions. The entire native 4:3 render is also found pixel-exactly inside both widened baseline images. That join and the original water ripple are preserved.

Private evidence is in `build/repros/water-new-junction-20260906/`:

- Supported ROM hash is unchanged. State SHA-256: `1481c73b38470ed631d9f0e4cf03fd218e34d535ec4260776d6e54d92f1ff392`. Baseline app: `551cbb65fd79c32e5bba1e169d57b66a913e495136fb61059992e0337a9238cc`. Updated normal app: `a260e17f393d691fcc1e50daead6f79dc971a07d646896faeb5d7d0f26223503`. `manifest.json` and `final-manifest.json` retain identities and local app options.
- Exact-state composite A/B changes **2,722 pixels at 16:10** and **8,063 at 16:9**, entirely to the right of the original image. Isolated BG1 changes 2,741/8,082 pixels; sprites occlude 19 of them in the composite. BG2, BG3 and OBJ are identical. Native 4:3 is identical on all five surfaces. Three candidate layer repeats agree.
- **Glide placement matters:** this save has bias 11 at 16:10 and 28 at 16:9. In both images the original 256-pixel viewport occupies output X15–270. The nominal centered crop includes some added right-side art and is not the correct oracle here. Exact and per-frame tests compare the entire authentic viewport plus every pixel to its left, using `extra - presentation_bias` for its origin. No native pixel changes.
- Explicit BG1 occupancy checks turn the lower-hole rectangle from **0/1,100 to 1,100/1,100** covered pixels and the outer-strip rectangle from **0/312 to 312/312**. `occupancy.json` gives their exact source-frame coordinates. This supplements integrity checks with direct assertions on both reported holes.
- Stay, Left, Right, Down, Up and Up-Left routes run at both wide aspects, with three identical candidate repeats against the preserved prior executable. Across **5,340 frames per replay set**, every original-viewport/left pixel and all WRAM/VRAM/CGRAM/PPU-OAM/WRAM-OAM hashes remain exact. The upward routes reach Y9568; leftward routes reach X16 and leave the correction. Right/Down encounter the alcove wall/floor and are not counted as traversal. `validate.py`, input files, frame sequences and `validation-summary.json` preserve the schedules and results.
- The preceding camera audit's **50 movement branches (18,000 frames)** retain every regional and guest-memory hash. The three earlier user seam/hole saves are exact at 16:10, 16:9 and native width. The transition sentinel passes **34 samples/five boundaries**. Four available clean entrances retain zero hard failures and four pre-existing native/wide divergence investigations.
- **241 unit/model tests**, no failures, one skip. The model mutates or removes every relevant source/donor cell and checks orientation/native bounds, row/column limits and null output pointers. Host/headless builds, strict signing and whitespace checks pass.
- The private candidate and rebuilt **normal app** were inspected at the preserved save with Reconstruct mode 3, strength 100, softness 86 and shading 99. `candidate/normal-app-window.jpg` shows both right-edge repairs. The normal app was closed gracefully, updated and reopened at the user's current quicksave; the slot and preferences were preserved. No automated input schedule remains in the normal app.

A clean Coral Capers pre-entry anchor is still unavailable; the four other entrance tests cannot substitute for it. This validates the supplied state and the tested alcove paths, not every aquatic room or all 40 entrances. Source defaults remain off, and no commit or release was made.

## Upper western shaft: recurrence after swimming higher (2026-09-06)

The next tester save is frame **553153**, mode 3, level `$0061`, entrance
`$00BF`, camera **16/9380**, 16:10/Glide/composite. Its SHA-256 is
`2cad91a69c4e27006a9658788e1bfae727d99162ec4147e1a116bf1425dff7ac`.
The immutable input, supplied screenshot, actual original app window, prior
executables, raw planes/memories, and scripts are in
`build/repros/water-upper-alcove-20260906/`. This is a new section above the
previous Y9568 traversal limit, not evidence that the lower correction failed.

**Presentation cause and containment.** Metatile rows 295..298 end at column
8. Columns 9..11 are unused zero cells; the general adjacency rule fills only
part of them, exposing two rectangular BG2 holes. Retained and cold-load
frames are identical at all three aspects, so this is not serialized cache
corruption. A separate `Dkc1CoveShaftSourceMatches` capability under the existing
`DKC1_WS_WALL_SEAMS=1` checks 24 target/guard cells at E9:9310..9596 and 12
source cells at E9:8C10..8D16. The donor rows 280..282 have exactly the same
retained edge cells (`40C5`, `40C9`, `40D9`) and form the correctly phased
upright strip. Only columns 9..11, rows 295..298, with native right edge 8
can use them. The host still checks vertical layout, E9/D0 banks and zero map
bases before using the capability. No cartridge tile, camera, object or
collision writes change. General adjacency and the lower-alcove capability
are unchanged; an unverified scene still cannot borrow these strips.

The atlas was attempted first, but this checkout lacks
`reference/disassembly/DKC1/Pseudocode/instruction_index.csv` and the nested
disassembly directory. The supported clean-ROM bytes and independent map
renderer are the data oracle; no external or generated source was edited.
The ROM remains SHA-256
`fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15`.

**Validation and actual coverage.**

- Exact A/B changes **3,342** BG1/composite pixels at 16:10 and **6,640** at
  16:9. BG2/BG3/OBJ remain identical. All five native-width surfaces remain
  identical. With Glide, the original image is output **X2..257** at both
  wide aspects; that entire viewport and all pixels to its left are exact.
  Three independent layer captures agree. The opt-in disabled remains
  byte-identical to the prior build at all three widths.
- The explicit BG1 hole probes pass: `(277,60)-(304,88)` goes from **0/756**
  to **756/756** occupied pixels; `(277,124)-(304,184)` goes from **0/1620**
  to **1620/1620**. See `occupancy.json` and `layer-comparison.json`.
- **22 route/aspect cases**, **23,700 frames per replay set**, compare against
  the preserved prior executable and repeat three times. Every frame preserves
  the Glide-positioned original image/left pixels and all five guest-memory
  hashes. Final state, frame, WRAM, VRAM and complete traces repeat exactly;
  audio hashes match the baseline and all repeats. Terrain trace deltas report
  zero BG1 misses or raw fallback. Scripts and inputs, `validation-summary.json`,
  `shaft-shuttle-summary.json`, `audio-comparison.json`, and
  `terrain-integrity.json` preserve the evidence.
- Actual camera coverage reaches **Y8996** above the save and **Y10048** below
  it across the branches. The shaft-return route crosses Y9380 upward at
  relative frame 63 and downward at frame 598, while keeping X16. Later pulses
  encounter terrain below the shaft: this is **one completed out-and-back**,
  not three full shaft traversals. The `lower_to_shaft` branch from the prior
  alcove save is blocked at Y9752 and does not connect the two tester roots;
  its name is retained in raw evidence, but it is only a lower-area regression.
  Other discovery attempts likewise do not establish that connection.
- The **50 preceding movement routes / 18,000 frames** retain all regional
  and guest hashes. The **four earlier tester saves** are byte-identical at
  native, 16:10 and 16:9. The transition sentinel passes **39 samples across
  six boundaries**. Four available fresh entrances pass their hard gates,
  retaining four previously known native/wide investigations.
- **241 unit/model tests**, zero failures, one skip. The shaft model mutates
  and removes every target/donor/guard cell and checks flips, native bounds,
  coordinate limits and null outputs. Headless/normal-host builds, strict
  signing and `git diff --check` pass.
- Actual private app windows were inspected at the supplied save, after the
  climb at relative frame 120, and on the return at frame 598. The selected
  Reconstruct mode 3/strength 100/softness 86/shading 99 and original ripple
  are retained. `candidate/qa-*-window.jpg` captures the visible result.

A Select-from-pause attempt did not leave Coral Capers. A clean Coral pre-entry
anchor remains unavailable, and the 40-entrance floor still reports 36 missing
anchors. These results validate the exact state and documented paths; they do
not promote the capability globally or claim all aquatic cameras are repaired.
Source defaults remain off and no commit or publication was made.

Prior normal executable: `a260e17f393d691fcc1e50daead6f79dc971a07d646896faeb5d7d0f26223503`. Updated normal executable: `06c8dad0bffaf9a268ef64b09d875c217c821a1df6848dd416d87b4ab023e618`. Updated headless: `d676c28620fdeb860abc191c6197b297ee40189ce568afe92a6baa2c01f70e79`. `manifest.json` retains the identities.

The normal app was then closed gracefully, updated, signed and reopened at the
unchanged tester quicksave. `candidate/normal-app-window.jpg` verifies both
holes are closed in the actual normal window. Its Info.plist and user settings
are unchanged; no diagnostic schedules or snapshot overrides were installed.
The private QA app was restored to its immutable root and closed.
`final-manifest.json` records the live normal process, build and preserved slot.
