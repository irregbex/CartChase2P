# Windows dependency provenance

The Windows frontend uses these unmodified, pinned dependencies. CMake fetches
sources into the ignored build tree; no dependency binaries are tracked.

| Dependency | Revision | License | Use / adaptations |
| --- | --- | --- | --- |
| [SDL 2.30.9](https://github.com/libsdl-org/SDL) | `c98c4fbff6d8f3016a3ce6685bf8f43433c3efcc` | zlib, `SDL-LICENSE.txt` | Window, OpenGL context, audio, controllers; no source edits |
| [miniz 3.0.2](https://github.com/richgel999/miniz) | `293d4db1b7d0ffee9756d035b9ac6f7431ef8492` | MIT, `miniz-LICENSE.txt` | Statically linked bounded ZIP/MSU1 extraction; no source edits |
| [snesrecomp fork](https://github.com/elliotttate/snesrecomp) | `851b11e38588818afc705e4270d3eb982b7b2af2` | PolyForm Noncommercial 1.0.0 | Existing submodule, unchanged by this port |

The Windows shader generator translates this repository's existing
`runner/macos_graphics.metal` arithmetic mechanically into GLSL 3.30. Its
DKC2Recomp/MIT provenance remains in `THIRD_PARTY_NOTICES.md`. It introduces no
third-party shader source or extracted game imagery. AppKit, Metal and the Mac
build remain separate and unchanged.
