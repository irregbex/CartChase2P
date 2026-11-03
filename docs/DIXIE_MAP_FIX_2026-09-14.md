# Dixie map repair - September 14, 2026

The reported Ropey Rampage map corruption is fixed in the rebuilt Windows
Dixie variant. Fresh boot, normal map reload, Jungle/Ropey map navigation and
Ropey level entry are clean in the inspected native frames. The actual SDL
window was inspected and left paused on the refreshed user map. Full-game
and widescreen certification are outside this result.

## Reproduction and cause

The supplied quicksave was preserved before any operation as
`build/dixie-map-fix-20260914/user-map.state`, SHA-256
`265c1760e32ea5ae396e9fa4c57adbf1de58e18730693247320cb238581cdd71`.
It is absolute frame 13000, mode `$0000`, entrance `$000C` (Ropey Rampage
selected on the map), native 256x224, six lives and 83 bananas. Its baseline
framebuffer hash is
`56ae4a7477be98d7997cdaf46512c8387365296822670e656340751ab9952aeb`.
The original save remains immutable outside the active slot. All evidence
paths below are relative to the private ignored directory
`build/dixie-map-fix-20260914/` unless otherwise stated.

The first tracked fresh Jungle-map corruption in the previous QA occurs at
absolute frame 5728; earlier corruption is visible on the preceding map.
This is not claimed as the earliest failure in the entire game. The exact
user state and independent controller-only boot both reproduce the defect.

The affected domain is cartridge data addressing. The pinned mod's pose-table
entry at `$BB:F8A8` contains `00 60 41 00`. Runtime instruction/WRAM watches
show the sprite layout decoder using pointer `$41:6000` (and `$41:6800` for
another pose). The bytes at file offset `0x416000` begin
`00 06 00 00 00 06 00 00 78 70 80 70 78 78 80 78`. The previous universal
HiROM fold resolved that address to `0x016000`, whose unrelated bytes begin
`01 00 FF 00 FF 00 FF 00`. Those bytes were consumed as sprite layout
counts/coordinates and graphics transfer metadata. See `oracle-sprite.jsonl`,
`watch-exact.jsonl`, `profile-exact.jsonl` and the exact/candidate raw captures.

The documented local atlas source library is absent in this checkout; the
atlas lookup was attempted first. A private clone of the
[upstream DKC1 disassembly](https://github.com/Yoshifanatic1/Donkey-Kong-Country-1-Disassembly)
at `c2080f40469c716923f550706509a0d354229841` supplied navigation aids.
Pointer/header claims above were checked against local ROM bytes and runtime
values, not inferred from symbolic names alone. `reference/` was not changed.

## Correction and save handling

`snesrecomp/runner/src/snes/cart.h` exposes the actual bounded HiROM resolver
and a host-selected expanded-data capability. `cart.c` resets the capability
on cartridge load and delegates HiROM pointer resolution to it. Only
`runner/dkc1_game.c` compiled with `DKC1_DIXIE_VARIANT` enables it, after
verified cartridge loading and before reset. `$40-$7D` then address linear
file data when within the image; out-of-range reads do not wrap. `$C0-$FF`
keep their stock aliases. Stock HiROM remains unchanged by default. The flag
is cartridge configuration and is not serialized into guest state.

The prior theory that the extension should replace CPU bank `$C0` was wrong:
those aliases are used by original code/data. The observed missing layouts
are in low banks `$40-$41`. No generated source, PPU filtering, widescreen
policy, guest-memory rewrite or new DMA workaround was added. The existing
variant DMA workaround and unrelated dirty engine files were preserved.

Correct addressing removes the large bad sprite clusters immediately, but
the exact historical snapshot retains already-written bad VRAM until ordinary
scene initialization. Its preserved copy is still historical evidence.
`recipes/dixie-map-refresh.dks` refreshes through controller input alone:
move to completed Jungle Hijinxs, enter, Start-pause, wait for debounce,
Select-exit normally, then move back to Ropey Rampage. This takes 1974 frames.
It avoids death and consumes no lives or bananas. The refreshed map is absolute
frame 14974, mode `$0000`, entrance `$000C`, native 256x224.

Three runs produced the same complete refreshed state, SHA-256
`8fdcc1865113319b9995c0f08cd4ca5a477f218f20cc1114ecd521cfcb2cc32e`.
After checking it, this state replaced the active
`build/dixie-qa-20260913/sdl-user/quicksave.state`. Completion flags, six lives,
83 bananas, selected Kong, selected entrance and unlocked named map nodes
match the supplied root. A transient unused map-cache entry changes during
normal reload; the entire WRAM image is not expected to remain identical.
`final-validation.json` records the comparisons. `final-live.json` records
the launched build, slot, original backup, paused state and absent schedule.

## Validation and reproducible commands

All four stock/Dixie host/tool builds and both Windows SDL targets succeeded.
The Python suite ran 244 tests, OK with ten skips; the new synthetic mapper
test compiled and ran with MSVC `/W4 /WX`. It checks real resolver boundaries,
expanded sprite addressing, stock aliases and default-off behavior without
ROM assets. All three Windows CTests passed. Skips are not counted as coverage.
The repository diff and the two changed cartridge files pass whitespace
checks. A full submodule diff check still reports pre-existing CRLF whitespace
in the unchanged DMA workaround (`dma.c`/`dma.h`); that unrelated work was
preserved, and a full submodule whitespace pass is not claimed.

| Check | Result and evidence |
|---|---|
| Exact original state | Baseline corruption and candidate residual historical VRAM inspected; `exact-0/`, `exact-60/`, `candidate-exact-1/`, `candidate-exact-120/` |
| Fresh boot map | First 5750 frames of the existing Jungle startup recipe; clean map in `candidate-fresh-map/` |
| Normal map reload and navigation | Three checkpoints across three byte-identical independent runs, including framebuffer/audio; zero integrity violations; `map-regression.json`, `map-regression.log` |
| Existing Jungle entry and quickload | Both legs pass three identical repeats; `regression.json`, `regression.log` |
| Next level | B for six frames, neutral for 594 from the refreshed map enters Ropey; clean rain/rope gameplay in `final-next-level/` |
| Jungle spin unchanged | 120-frame grounded spin matches previous accepted framebuffer and all five memory hashes; `final-spin/`, `final-validation.json` |
| Stock output unchanged | Independent stock fresh startup matches its previous framebuffer and all five memory hashes; `stock-fresh-reference/` |
| Save continuity | 240 frames split at 37, v9, restored and uninterrupted results match; `save-continuity/report.json` (tool uses wide mode; continuity proof only) |
| Actual visible application | Fixed SDL window paused on clean Ropey map; `final-visible-after.jpg`, `final-visible-accessibility.json` |

From an x64 Visual Studio developer shell with `$rom` pointing to the clean ROM:

```powershell
.\build_host.bat
.\build_host_dixie.bat
.\build_host_tools.bat
.\build_host_dixie_tools.bat
cmake --build build-windows/release --parallel 6
ctest --test-dir build-windows/release --output-on-failure
$env:PYTHONDONTWRITEBYTECODE = '1'
python -m unittest discover -s tests -v
$env:DKC1_DIXIE = '0'
$env:DKC1_WIDESCREEN = '0'
python tools/run_regression.py contracts/dixie-jungle.json --exe build/dkc1_dixie_headless.exe --rom "$rom" --work build/dixie-map-fix-20260914/regression --json-out build/dixie-map-fix-20260914/regression.json
$env:DKC1_SAVESTATE_INPUT = (Resolve-Path build/dixie-map-fix-20260914/user-map.state).Path
python tools/run_regression.py contracts/dixie-map-refresh.json --exe build/dkc1_dixie_headless.exe --rom "$rom" --work build/dixie-map-fix-20260914/map-regression --json-out build/dixie-map-fix-20260914/map-regression.json
Remove-Item Env:DKC1_SAVESTATE_INPUT
git diff --check
git -C snesrecomp diff --check -- runner/src/snes/cart.c runner/src/snes/cart.h
```

The map contract requires this exact immutable root; its predicates are not
a substitute for checking the recorded root hash. Reproductions should use
new output directories to preserve the recorded evidence. Private `probe.py`
and `final-validation.py` record the remaining exact input schedules/captures.

## Identities and limits

Checkout: `aa19639` plus the preserved existing work and this fix. No release
archive, publication or commit was made. `final-manifest.json` records source,
recipe, build, state and evidence identities. ROMs, generated sources, private
states and extracted art stay outside commits.

| Item | SHA-256 |
|---|---|
| Clean ROM | `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15` |
| Pinned synthesized mod | `2769b72a8a2050000336f5dd6dea1a45385f4f35ee710dafb0c0a3592295643b` |
| Final Dixie headless | `d71b7c8a4cc9b1a9d1ad51eb72e9043bcb92f0d1b023ea9376bad820c36ea35c` |
| Final Windows SDL Dixie | `75fe52b79b4f5487c0980c084863ec3b3a6be140afc8eef9cd6c81b52a81a067` |
| Refreshed framebuffer | `539195c79d80035a6f4e987e3b7f2d4096b2b7dc722f9b242b3ffda9eebb773f` |
| Refreshed WRAM | `bdd3e7616e152f29019b4991a2e49444cded8cc7464e105ff21d9e4ecfcbcc5e` |
| Refreshed VRAM | `318536d33b31f26cc7e3c6843b14de9675eff9abc2eb6a1327748a27a4c3cb09` |
| Refreshed PPU OAM and WRAM shadow | `78934a6caf50130892ea4c27c90f0ae6e62132db1b83013ee5c5ba73c2abb0dc` |

The tested scope is native Jungle and Ropey map/gameplay branches, not every
map, full level completion, bosses, underwater, animal buddies, save-file
progression, physical-controller rumble or macOS. No shared presentation or
widescreen capability was promoted. The 40-entrance matrix and retained/cold
transition sentinel were not run; the normal controller entry/exit transitions
above were exercised directly. Old snapshots can retain historical corruption
until a normal reload; no broad state repair is claimed.
