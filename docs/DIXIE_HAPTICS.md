# Dixie enemy-stomp haptics

The Windows host already had default-on enemy-stomp rumble, but no visible
control. It also compared Dixie's rebound against stock DK's value, so it
rejected a genuine Dixie stomp. v0.0.13 adds the Game-menu toggle/test command
and selects the proven rebound value only in `DKC1_DIXIE_VARIANT` builds.
Stock DK and Diddy retain their previous values and all other detector guards.

Clean and pinned-mod ROM bytes at CPU `$BF:A79F` (file `0x3FA79F`) differ:
stock `20 07` encodes `$0720`, while Dixie `00 09` encodes `$0900`. This is the
immediate loaded by the accepted enemy-stomp routine at `$BF:A79C`; Diddy still
uses `$0880`. The atlas was checked first; this checkout lacks its source
index. The previously preserved upstream disassembly was used for navigation,
and the claim was checked against actual ROM bytes and live WRAM.

From the immutable grounded Jungle root
`build/dixie-qa-20260913/idle/final.state` (SHA-256
`1693867240ad451de4fc44477c1ec1c3109881aec9d2ed4159ea1bc1a720b97c`),
the input `80*105, 81*24, 80*200, 0*100` is walking right, jumping right,
continuing right and settling. At relative frame **149** (absolute 7750),
actor slot 2/id 1 stays in state 1 while vertical velocity changes from
`$F8B8` (-1864) to `$0900`. The old detector rejects this; the variant-aware
detector accepts it. The ordinary jump at frame 106 changes state 0 to 1
and uses `$0780`, so it remains rejected. Raw frames and input sweeps are
under `build/release-v0.0.13/haptics/`; no state or ROM is committed.

The checksum-indexed 300-frame dump verifies with `tools/verify_wram_dump.py`;
its SHA-256 is `633c6c327789f58a791a2c19dcfeca86c2c359defe47230f52b3c0d4c3aafd7c`.
The real C detector, compiled both ways and fed that dump, reports zero stock
hits and exactly one Dixie hit at frame 149. The separate fresh-boot route
`recipes/dixie-stomp.dks` and `contracts/dixie-stomp.json` pass four checkpoints
over three byte-identical runs: grounded, falling, stomp, and landed. Landing
is at absolute frame 7910 with five lives preserved. The stomp WRAM SHA-256 is
`3c173acc3f2d59405dc1e8d1f6148273f3d2b91ae7ea5b77cfe65dd7e4d67149`;
VRAM is `da9799181a44ffad26c01dd82c7afcac9bfa3900891b29ff7358985e13c91b4e`.
The regression report and all memory/image hashes are in
`build/release-v0.0.13/stomp-regression/`. Run it with the Dixie headless host
and the clean ROM, without `DKC1_SAVESTATE_INPUT`, using `tools/run_regression.py`.

The public native detector test compiles stock and Dixie separately and checks
the distinct impulses, Diddy's unchanged rebound, ordinary jumps, damage,
already-rising motion and actor changes. Windows platform tests check the
persisted toggle and menu state. `--haptics-test` routes a pulse and stop through
the actual worker and SDL virtual-controller callback; disabled feedback must
not issue a pulse. This proves software output routing, not physical vibration.

**Game > Controller rumble (enemy stomps)** defaults on and takes effect
immediately. **Game > Test controller rumble** sends the existing 55 ms pulse
to player one's supported controller, or explains that a compatible device is
needed. The preference lives in `windows.ini` under `[Host] Haptics`; the
existing `DKC1_HAPTICS` environment setting overrides it at startup. Disabling
stops active rumble. No cartridge timing, movement, collision or save format
changes are made.

The device inventory during this Windows check reported no SDL game controller.
Physical rumble remains unverified; keyboard mappings from another program
alone cannot receive SDL controller rumble. The paired old Mac binary is
unchanged and does not include these new controls or the Dixie correction.
