# BFME1 Mobile

**An open-source Android port of *The Lord of the Rings: The Battle for Middle-earth* (2004), with touch controls and multiplayer, built on the GPL v3 source code of the SAGE engine (*Command & Conquer: Generals – Zero Hour*).**

[Русская версия](README.ru.md) · [License](LICENSE) · [Credits and legal notice](NOTICE.md) · [Contributing](CONTRIBUTING.md)

> [!IMPORTANT]
> This repository contains **engine source code only**. It contains no game data from *The Battle for Middle-earth*: no `.big` archives, models, textures, maps, videos, music or text.
> To play, you need your **own legally purchased copy** of BFME1 (v1.03 with the 2.22 community patch). The app copies the game files from your copy to the phone on first launch.

> [!NOTE]
> The project is a work in progress and is **not playable yet**. BFME1 maps load in the engine, but starting a match on the phone, combat, AI and the original menus are still being built. See [`docs/GAMEPLAY-GAP.md`](docs/GAMEPLAY-GAP.md) for the current state.

## What it is

BFME1 runs on SAGE, the same engine as *Command & Conquer: Generals – Zero Hour*. In 2025 Electronic Arts released the Generals/Zero Hour source code under GPL v3, and the community has since ported it to modern 64-bit platforms. This project takes the community Android port of Zero Hour and teaches the engine to read and play BFME1 data:

- **Engine:** the BFME1 file formats (BIG, INI dialect, maps, W3D, strings) and gameplay systems on top of the Zero Hour engine.
- **Mobile:** arm64 Android 10+, OpenGL ES 3.0 by default (Vulkan optional), a touch-first HUD.
- **Multiplayer:** deterministic lockstep with a relay server, built to be replay- and CRC-testable.

## Status

| Area | State |
|---|---|
| BFME1 data loading (BIG/VFS, INI, maps) | works, covered by tests |
| Touch controls (from the Zero Hour Android port) | works |
| Starting a BFME1 map on device | in progress |
| Units, combat, AI | in progress |
| BFME1 menus and HUD | in progress |
| Multiplayer | designed ([`docs/NET-DESIGN.md`](docs/NET-DESIGN.md)), not implemented |

Primary test devices are Google Pixel phones (Tensor G1–G5).

## Building

All builds run in Docker and need no GPU.

| Target | Command | Docs |
|---|---|---|
| Android APK (arm64-v8a) | `tools/build-apk.sh` | [`docs/bfme/ANDROID_BUILD.md`](docs/bfme/ANDROID_BUILD.md) |
| Linux x86_64 (engine debugging, DXVK → Vulkan, SDL3) | `tools/build-linux.sh` | [`docs/bfme/LINUX_BUILD.md`](docs/bfme/LINUX_BUILD.md) |
| Headless smoke test on Mesa lavapipe | `tools/build-linux.sh --smoke-only` | same |

The repository is large. A shallow clone is enough to build:

```sh
git clone --depth 1 https://github.com/KatoteKiru/bfme-mobile.git
cd bfme-mobile
git submodule update --init --depth 1 references/fbraz3-dxvk
```

APKs are signed with a key that lives outside the repository (see the Android build doc). No signing keys or service credentials are ever committed.

## Repository layout

| Path | Contents |
|---|---|
| `engine/bfme/` | BFME1 gameplay layer on top of the engine |
| `formats/` | BIG/VFS, BFME1 INI schema, MAP, W3D, STR/CSF, textures |
| `net/`, `server/` | lockstep, transport, relay, lobby |
| `ui/` | mobile HUD and menus |
| `tests/` | unit, replay and per-frame CRC tests (synthetic fixtures only) |
| `tools/` | build scripts and `tools/datacheck` (BFME1 data inventory, runs against your own copy) |
| `android/` | Gradle/NDK app shell |
| `docs/` | design notes, architecture decisions (`docs/adr/`), build docs |
| `Core/`, `Generals/`, `GeneralsMD/`, … | engine code inherited from upstream |

## Upstream

This is a fork of community work, and none of it would exist without them:

```
EA source release (GPL v3, 2025)
  → TheSuperHackers/GeneralsGameCode
    → Fighter19/CnC_Generals_Zero_Hour → fbraz3/GeneralsX
      → ammaarreshi/Generals-Mac-iOS-iPad
        → MYSOREZ/GeneralsZH-Android-Port
          → KatoteKiru/bfme-mobile (this repository, branch bfme)
```

The upstream README is kept as [`README-upstream.md`](README-upstream.md). Full credits are in [`NOTICE.md`](NOTICE.md).

## License

GNU General Public License v3.0, the same as the EA source release it derives from. See [`LICENSE`](LICENSE). [`LICENSE.md`](LICENSE.md) is the same text with the additional terms from the original EA release, which still apply.

## Legal

BFME1 Mobile is an unofficial, non-commercial fan project. It is not affiliated with, endorsed by, or sponsored by Electronic Arts Inc., Warner Bros. Entertainment, New Line Cinema, Middle-earth Enterprises or the Tolkien Estate. *The Lord of the Rings*, *The Battle for Middle-earth*, *Command & Conquer* and related names and logos are trademarks of their respective owners. No game assets are distributed by this project. See [`NOTICE.md`](NOTICE.md).
