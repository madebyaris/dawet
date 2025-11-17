<div align="center">

# Dawet 🍹  
**WhiskyWine roots • CrossOver strength • Cendol spirit**

[![SwiftLint](https://img.shields.io/github/actions/workflow/status/madebyaris/dawet/SwiftLint.yml?style=for-the-badge)](https://github.com/madebyaris/dawet/actions)
[![Discord](https://img.shields.io/discord/1115955071549702235?style=for-the-badge)](https://discord.gg/CsqAfs9CnM)

</div>

## Welcome to Dawet

This repo is the spiritual successor to WhiskyWine—rebuilt and maintained by [madebyaris.com](https://madebyaris.com) / [github.com/madebyaris](https://github.com/madebyaris).  
We still stand on the shoulders of CrossOver and the pioneering Whisky community, but Dawet is evolving into something more robust, more modern, and laser‑focused on day‑to‑day macOS users.  
Internally we call this effort **Project Cendol**: keep the drink sweet, refreshing, and reliable for everyone.

## What you get

- **CrossOver-grade compatibility** – Dawet embeds CrossOver 22.1.1 patches plus Apple’s Game Porting Toolkit, so DirectX 11/12 titles get serious love.
- **WhiskyWine heritage** – The SwiftUI bottle manager you already know, refined and rebranded.
- **Local build friendly** – `Scripts/build-all.sh` compiles Wine 10.0, DXVK 1.10.3, MoltenVK, cabextract, etc. Drop your custom artefacts in `Libraries/` and Dawet will pick them up via `DAWET_SOURCE_ROOT`.
- **End-user comfort** – One-click Steam installation, pinning programs, Winetricks integration, and automatic detection of your local Wine build so you’re not stuck downloading “WhiskyWine” ever again.
- **Cendol roadmap** – A constantly updated checklist of UX upgrades (automatic health checks, curated presets, better crash reporting) built from real-world feedback.

## System requirements

- Apple Silicon Mac (M1, M2, M3…)
- macOS 14.0 (Sonoma) or newer
- ~40 GB free disk space for source builds (less if you use prebuilt artefacts)

## Installation

| Method | Command |
| ------ | ------- |
| Homebrew | `brew install --cask dawet` |
| Manual build | `./Scripts/build-all.sh && open Dawet.xcodeproj` |

When running from Xcode, set an environment variable `DAWET_SOURCE_ROOT=/path/to/dawet` so the app uses the local libraries you compiled.

## Usage snapshot

```bash
# clone and build everything (Wine, DXVK, MoltenVK…)
git clone https://github.com/madebyaris/dawet.git
cd dawet
./Scripts/build-all.sh

# launch in Xcode
open Dawet.xcodeproj
```

Create a bottle, click **Install Program → Install Steam**, and Dawet will download the latest Steam installer, run it through Wine, and surface the app in “Installed Programs” automatically.  
If Steam throws the classic `0x3008` error: add `-no-cef-sandbox -tcp` to Steam’s arguments or run `winetricks corefonts`.

## Support the maker

Dawet is crafted with ❤️ by [madebyaris.com](https://madebyaris.com). If this project saves you time or helps you ship games, please consider [sponsoring on GitHub](https://github.com/sponsors/madebyaris). Sponsors keep the Cendol roadmap moving, pay for test hardware, and let me spend more time fixing the hairy Wine bugs nobody else wants to touch.

## Credits & thanks

- Whisky / WhiskyWine team – for the original UI and community spark.
- [CodeWeavers & CrossOver](https://www.codeweavers.com/crossover) – upstream patches and decades of Wine expertise.
- [DXVK](https://github.com/doitsujin/dxvk), [MoltenVK](https://github.com/KhronosGroup/MoltenVK), [msync](https://github.com/marzent/wine-msync), Sparkle, SemanticVersion, Swift Argument Parser, SwiftTextTable, and the macOS gaming scene at large.
- Apple’s D3DMetal / Game Porting Toolkit for the Metal backend goodness.

And of course, everyone filing issues, sending PRs, and showing up in Discord. Dawet is a community drink—cendol in spirit, robust in delivery. Cheers! 🥂
