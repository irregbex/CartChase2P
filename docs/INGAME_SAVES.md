# In-game saves and SRAM persistence

## v0.0.12 repair

Earlier desktop hosts allocated the cartridge's 2,048-byte SRAM but never
loaded or wrote its battery file. Candy saves disappeared on process restart.
Save states worked because snapshots include SRAM. This was a host storage
defect; cartridge instructions, dispatch, checksums, and widescreen behavior
are unchanged.

`runner/desktop_sram.c` loads the complete file after cartridge initialization
and before the first guest frame. The shared Windows/macOS SDL host and legacy
Win32 host check for changes after each successful emulated frame and flush at
graceful exit. Failed emulation frames are not flushed. The headless runner
retains explicit `DKC1_SRAM_INPUT` and never implicitly writes a battery file.

Writes use a temporary file in the same directory, flush it to disk, and
atomically replace the previous file. Windows uses `MoveFileEx` with replacement
and write-through. A failed write keeps the previous file and retries after 60
frames or at shutdown. The desktop pauses and shows the first write error, so
the player can preserve a state or correct the storage issue. Unchanged SRAM
causes no writes. Missing files are normal; unreadable, truncated or oversized
files stop startup without modifying the file or SRAM.

Windows SDL saves live at
`%APPDATA%/Flat2VR/DKC1Recomp/saves/save.srm`. Mac SDL source uses
`~/Library/Application Support/Flat2VR/DKC1Recomp/saves/save.srm`.
`DKC1_USER_DIR` redirects the SDL user directory for private QA. The legacy
Win32 debugger uses `saves/save.srm` under its working directory; run automated
tests from an isolated directory. The battery file contains all three cartridge
slots, separate from the five host save-state slots.

Loading an older state restores its older SRAM. After execution resumes or the
app exits normally, that becomes the battery file. To recover progress from an
earlier release, back up the state, load it in v0.0.12, visit Candy and save,
then exit and select the in-game file after a fresh launch. Progress held only
in a state before saving at Candy is not automatically a cartridge save.

## Evidence and scope

The original defect repeated three times in v0.0.11 Windows SDL executable
SHA-256 `1a2c19f53dee664dcf2f8ee381a7ff1f8ee01f6072ee53fc9ce2ce2a7c24129c`:
SRAM survived snapshots, graceful exit created no battery file, and startup
ignored a seeded file. Explicit headless SRAM import preserved those bytes.
The ROM remains locked to SHA-256
`fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15`.

The cartridge fixture starts with the clean controller route to the first
Kongo Jungle map at frame 7,213. `tools/poke_test.py` changes only entrance
`$003E` from `$0016` to `$00FA` in a private copy to select Candy's destination.
This is a controlled scene-entry fixture, not controller-only campaign coverage.
The next 478 input frames execute stock entry and barrel-saving behavior:

```text
8 * 8
0 * 240
80 * 20
81 * 30
0 * 180
```

Candy's save screen shows slot 1 at 00:00 / 00%. The game produces SRAM SHA-256
`d07d87bb3767681d50c531267f3dd0a89fc1e1ffd41f7164ba77368c5f2c5b2a`.
The cold restart waits 5,400 frames, presses Start for six, waits 120, presses
Start for six, waits 1,800, then requires `$003E == $00FA`. The game returns
to Candy's saved map location without loading a state.

`tools/verify_ingame_saves.py` repeats desktop saving and cold restarting three
times in fresh user directories, comparing with headless cartridge oracles.
Disk SRAM must be exact. Save-leg snapshot comparison excludes only the
four-byte `Dsp.sampleRead` host playback cursor at `$183C0`. The cold-start leg
also excludes the host PCM output queue and its counters at `$103BC..$183C3`,
because the SDL unthrottled startup route resets that queue before opening audio.
`tests/ingame_snapshot_layout.c` verifies both ranges against the pinned C
structures and serialization order. All other bytes, including guest CPUs,
DSP synthesis, WRAM, VRAM, CGRAM, OAM, SRAM and game state, must agree. Raw
snapshot hashes are retained alongside comparison hashes.

```powershell
python tools/verify_ingame_saves.py --rom <private-ROM> `
  --exe build-windows/release/DKC1Recomp.exe `
  --headless build-windows/release/dkc1_snesrecomp_headless.exe `
  --entry-state <immutable-pre-Candy-entry.state> `
  --input <fixed-478-frame-input> --frames 478 `
  --restart-script <restart.dks> --out <new-private-evidence-directory>
```

Filesystem tests cover first launch, exact reload, unchanged data, replacement,
failed-write preservation, forced retries, and malformed inputs. Private logs,
states, inputs and hashed reports are under `build/save-qa/` in the isolated
release worktree. No ROM, save or generated cartridge code is committed.
Windows release/native/tool hosts are built. Mac source shares the fix, but no
new Mac binary or Mac runtime validation is part of v0.0.12. This is targeted
save validation, not a full-game or widescreen-matrix claim.

Release checks: 245 public tests (10 expected skips), three Windows CTest
suites, native and debugger builds, and `git diff --check`. The direct MSVC
build lists also include the already-existing Baby Kong animation source that
was missing from those lists; the CMake release already included it.
