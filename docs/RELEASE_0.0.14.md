# DKC1Recomp v0.0.14 — iOS touch controls and upscaling

This release adds the native iPhone/iPad app. Its landscape game surface uses
the display's native pixel scale and a matching source width (418×224 on the
tested iPhone), with fixed SNES pixel proportions and transparent controls.

- Roll one thumb from Y onto B to run and jump. Separate simultaneous touches
  also work. Double-tap Y to toggle Hold, shown by a green ring and label.
- **Menu → Upscaling** offers Original pixels, Sharp Bilinear, Bilinear and
  Reconstruct. Reconstruct has five detail modes, Strength, Softness and
  Shading controls, a live paused preview and saved preferences.
- Import your own verified DKC1 USA v1.0 ROM through Files. Candy saves and
  host quick saves persist separately. Stereo audio, optional touch feedback
  and paired GameController support are included.
- The iOS 27 scene-lifecycle launch crash is fixed. Gameplay enters landscape
  automatically; supported wider scenes reveal additional source columns.

## iOS download and signing

`DKC1Recomp-v0.0.14-iOS-arm64-sideload.ipa` requires **iOS/iPadOS 16 or newer**
on an arm64 device. It is signed with the publisher's **Apple Distribution**
certificate, with its signature checked again after extraction. It is a
**sideload package that must be re-signed for your own device**, not an
App Store/TestFlight or universally installable ad hoc package. Import the IPA
into your preferred signing/sideloading tool and supply your own Apple ID or
development provisioning credentials. Installation/expiry rules depend on
that tool and your Apple account. Source builds can instead use `build_ios.sh`
with your own development team.

The public IPA deliberately omits the private provisioning profile and its
registered-device identifiers. Recipient-side signing supplies a new profile
and signature. The provisioned device build remains separate and was tested on
the connected iPhone. Apple's [registered-device distribution documentation](https://developer.apple.com/documentation/xcode/distributing-your-app-to-registered-devices)
explains why a signature alone does not authorize installation on every phone.

No ROM, private saves, snapshots, generated source, diagnostic entrypoints or
device profile is in the package. Licenses, a per-file SHA-256 manifest,
source/engine identities and IPA checksum accompany the download.

## Other downloads

The latest previously published desktop binaries accompany this release,
with their original filenames and checksums:

| Download | Version and scope |
| --- | --- |
| `DKC1Recomp-v0.0.13-Windows-x64.zip` | Windows Dixie/haptics update and persistent cartridge saves; unchanged |
| `DKC1Recomp-v0.0.9-macOS-arm64.zip` | Apple Silicon Mac graphics update; unchanged, ad-hoc signed and not notarized; no Dixie or v0.0.12 save fix |

These desktop assets were not rebuilt or renamed as v0.0.14. Earlier releases
and their assets remain available.

## Validation and limits

- 251 unit tests pass with one optional skip; iOS arm64 device and simulator
  Release builds pass. Diagnostics are off in the released IPA.
- All 12 existing GPU oracle cases match macOS byte-for-byte in the iOS
  simulator, including five Reconstruct modes and cache invalidation checks.
  Full-screen filters, the settings preview and transparent controls were
  visually inspected. A 15-second live Reconstruct run advanced Jungle entry
  successfully before restoring the paused immutable state.
- Three iOS and three desktop fresh-boot Jungle replays match framebuffer,
  WRAM, VRAM, CGRAM, both OAM hashes and audio, at native and phone widths.
  Exact-state native-image comparisons pass; 27 retained/cold samples across
  four transitions pass.
- Four available clean entrances pass mobile terrain/repeat checks. The full
  40-entrance floor remains incomplete; native-versus-wide machine-state
  differences remain investigations. No shared widescreen policy is promoted.
- Physical multi-touch feel, Bluetooth controllers, sustained phone GPU
  performance and whole-game/all-boss coverage are not exhaustively validated.

See [iOS build, controls and detailed evidence](IOS.md). The downloadable
`BUILDINFO.json` records the final committed source and signed package hashes.
