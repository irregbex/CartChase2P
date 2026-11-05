# Cart Chase 2P

[![Cart Chase 2P - 1 minute trailer](https://img.youtube.com/vi/I8Xh4tp_96U/maxresdefault.jpg)](https://www.youtube.com/watch?v=I8Xh4tp_96U)

> **🤖 100% vibe-coded.** Every line of this mod was written by
> [Claude Code](https://claude.com/claude-code) (Anthropic's AI). No code was
> written by hand: the human part was the idea, the design, describing it in
> plain language, playing every build and reporting what was broken.
> See [How it was made](#how-it-was-made-100-vibe-coded).

Two-player, split-screen mine cart racing built on
[DKC1Recomp](docs/DKC1RECOMP_README.md), a static recompilation of
Donkey Kong Country (SNES) to native C.

Two complete copies of the game run inside **one process**, one per half of
a 512x224 screen, with their CPU/RAM/PPU state swapped every frame.

- 200+ stages: seamless remixes of Mine Cart Carnage and Mine Cart Madness
- 8 world themes (graphics, music and enemies read from your ROM)
- Per-player rewind on crash, live timers, 1st/2nd, per-stage records
- In-game stage editor (mouse + keyboard) and random stage generator
- 1-player time trial, sudden death option, F10 swaps controllers

> **No ROMs. Nothing illegal.** This repository does not contain, provide
> or link to any ROM, game code, graphics, maps, music or save states, and
> ROM requests will be ignored. Cart Chase 2P is only a mod for one specific
> version of the game, **Donkey Kong Country (USA) v1.0**, and works only
> with a copy you legally own and supply yourself.
>
> Unofficial, noncommercial fan project. Not affiliated with, endorsed by or
> sponsored by Nintendo or Rare. Donkey Kong Country is a trademark of
> Nintendo. See [cart2p/LICENCIA_Y_AVISOS.txt](cart2p/LICENCIA_Y_AVISOS.txt).

## You need

- Your own **Donkey Kong Country (USA) v1.0** ROM, headerless, 4 MiB,
  SHA-256 `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15`
  (v1.1, v1.2, Europe and Japan are rejected)
- Windows 10/11 x64, Git, Python 3.11+, Visual Studio Build Tools (MSVC x64),
  CMake, Ninja

## Build

```powershell
git clone <this repository> CartChase2P
cd CartChase2P
copy <your ROM> rom\dkc1.sfc
.\cart2p\JUGAR.bat compilar release
```

`JUGAR.bat compilar` does everything, in order:

1. downloads the [snesrecomp](https://github.com/elliotttate/snesrecomp)
   engine at commit `75223f3` into `snesrecomp\` (PolyForm Noncommercial)
2. **verifies your ROM by SHA-256** (`tools\cart2p\verificar_rom.py`; a
   512-byte copier header is stripped into a clean copy)
3. generates the recompiled game code from your ROM (`generated\`)
4. builds `build-windows\release\DKC1Recomp.exe`
5. creates everything else the mod needs from your ROM
   (`tools\cart2p\preparar_desde_rom.py`): start states, rail profiles,
   world skins, the 206 compiled stages and the editor images

Check that your locally created files match the reference build
(SHA-256 fingerprints only):

```powershell
python tools\cart2p\preparar_desde_rom.py --verificar
```

## Play

```powershell
.\cart2p\JUGAR.bat                                   # menu, 1 = play
build-windows\release\DKC1Recomp.exe --cart2p
```

Each controller picks its side on the start screen. The in-game menus and
the `JUGAR.bat` menu are in Spanish; the documentation is in English.
`JUGAR.bat` menu: 1 play · 2 stage editor (browser) · 3 create random
stages · 4 compile recipes · 5 build the game (Release) · 6 prepare files
from the ROM.

| Doc | What |
|---|---|
| [cart2p/docs/CARRERA_2P.txt](cart2p/docs/CARRERA_2P.txt) | race rules, controls, options |
| [cart2p/docs/EDITOR_ETAPAS.txt](cart2p/docs/EDITOR_ETAPAS.txt) | stage editor (in-game and HTML) |
| [cart2p/docs/FORMATO_ETAPAS.txt](cart2p/docs/FORMATO_ETAPAS.txt) | recipe and compiled file formats |
| [cart2p/docs/MAPA_WRAM_2P.txt](cart2p/docs/MAPA_WRAM_2P.txt) | memory map used by the two-game race |
| [cart2p/etapas_fuente/LEEME.txt](cart2p/etapas_fuente/LEEME.txt) | stage recipe folder |
| [rom/LEEME.txt](rom/LEEME.txt) | ROM requirements and check |

## Where the mod lives

| Path | What |
|---|---|
| `runner/cart2p/` | the mod in C: two-game race, HUD, stages, editor, recipe compiler |
| `tools/cart2p/` | Python tools: ROM check, preparation, stage generator, editor server |
| `cart2p/etapas_fuente/` | stage recipes (JSON: column numbers and options, no game data) |
| `cart2p/states/*.dks` | input scripts that recreate the start states from a clean boot |
| everything else | DKC1Recomp (MIT) |

## How it was made (100% vibe-coded)

Cart Chase 2P is an experiment in vibe coding: the whole mod was programmed
by Claude Code from requests written in plain Spanish, over about four weeks
(first session 2026-10-06, first public release 2026-11-03).

| Who | What |
|---|---|
| Human | the idea, the design, every request, playing each build, bug reports, final decisions |
| Claude Code | all of the mod's code (about 13,000 lines: C, Python, JavaScript/HTML/CSS), the 206 stage recipes, tests, docs and the packaging for this repository |

How the work was organised:

- small steps, one task per session, with a test asked for after every change
- a project context file and a session log, kept up to date by Claude, so
  each new session picks up where the last one stopped
- fixed rules written once: never edit `generated\`, never delete anything
  without asking, the 2P mode always behind a flag that is off by default
- automatic checks instead of guesswork: a headless mode, a bot that drives
  the cart to the finish line, and A/B hashes proving the original game is
  untouched when the mod is off
- for hard bugs (audio, camera) Claude was asked for evidence first (memory
  dumps, frame captures) before proposing a fix

"100% vibe-coded" refers to the mod (`runner/cart2p/`, `tools/cart2p/`,
`cart2p/`). The base it runs on, [DKC1Recomp](docs/DKC1RECOMP_README.md) and
the [snesrecomp](https://github.com/elliotttate/snesrecomp) engine, is the
work of their own authors. It is open source, so anyone can read the code
and judge it; concrete bug reports are welcome in Issues.

## License

Mod code: MIT. DKC1Recomp: MIT ([LICENSE](LICENSE),
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)). The snesrecomp engine is
PolyForm Noncommercial, so **any build may only be used and shared for
noncommercial purposes**. Never share ROMs, `generated\`, `*.state`,
`cart2p\stages\` or executables.
