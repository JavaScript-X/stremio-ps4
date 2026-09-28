# Stremio for PlayStation 4

<p align="center">
  <img src="assets/branding/stremio-javascript-x-master.png" width="360" alt="Stremio PS4 logo with the JavaScript-X creator badge">
</p>

<p align="center">
  A native, controller-first Stremio client for jailbroken PlayStation 4 consoles.
</p>

> [!IMPORTANT]
> This is an unofficial community project created by **Tahar Chtioui
> ([@JavaScript-X](https://github.com/JavaScript-X))**. It is not affiliated
> with or endorsed by Stremio, Smart Code OOD, Sony, or PlayStation. Only use
> addons and media that you are legally authorized to access.

## Project status

Version **4.04** is under active development and has been tested on a real PS4
running firmware **13.02 with GoldHEN**. It is not a finished consumer release.
Back up important console data and expect compatibility differences between
firmware, payload, media container, codec, and addon combinations.

Working areas include:

- Native 1920×1080 OpenOrbis interface with DualShock 4 navigation.
- Android TV-inspired hero layout, expandable sidebar, and continuous catalog.
- Movies, series, public-domain content, search, details, and episode selection.
- Stremio device-link authentication and synchronized addon endpoints.
- Cached posters, wallpapers, title logos, metadata, and stream results.
- Torrent resolution through the official Stremio server on the local network.
- Direct Videodec2 H.264 playback with companion HLS audio through AVPlayer.
- Player HUD, pause, restart, fit/fill modes, and audio-sync adjustment.
- Packaged playback diagnostics for 360p, 480p, 720p, and 1080p sources.

Still planned or incomplete:

- Broad container and codec coverage, including reliable MKV/HEVC handling.
- Subtitles, library synchronization, Continue Watching, and progress sync.
- Multiple audio/subtitle-track selection and long-session recovery testing.
- Compatibility testing across more PS4 firmware and jailbreak environments.

## Architecture at a glance

The PS4 application owns the interface, account/addon client, caching, input,
and hardware playback. Torrent transport remains in the official Stremio server
running on a PC or NAS. Addons remain remote HTTP services; the PS4 does not
download or execute addon code.

```text
Stremio account and addon services
                |
                v
       Native PS4 application
       UI / cache / Videodec2
                |
                v
 Official Stremio companion on LAN
 torrent resolution / HLS delivery
```

See [Architecture](docs/architecture.md) for component and trust-boundary
details, [Roadmap](docs/roadmap.md) for priorities, and
[Console testing](docs/testing-m0.md) for the real-hardware checklist.

## Requirements

- A jailbroken PS4 capable of installing homebrew PKGs.
- GoldHEN or another compatible homebrew environment.
- Docker Desktop for the recommended reproducible build.
- The official Stremio server on a PC/NAS for torrent-backed streams.
- A legally obtained OpenOrbis toolchain when building without Docker.

No Sony SDK files or credentials are included in this repository.

## Build

### Reproducible Docker build

```powershell
./scripts/fetch-openorbis.ps1
docker compose build builder
docker compose run --rm builder
docker compose run --rm packager
```

The package is written to:

```text
dist/IV0000-BREW00100_00-STREMIOPS4000000.pkg
```

### Existing OpenOrbis installation

```sh
export OO_PS4_TOOLCHAIN=/path/to/OpenOrbis-PS4-Toolchain
make check
make
make package
```

The build uses OpenOrbis `_common/graphics.cpp` and runtime assets from the
configured toolchain. If your installation uses different paths, provide
`RIGHT_SPRX` and `ICON0` explicitly to `make`.

## Companion server

Start the official Stremio server on a computer connected to the same network:

```sh
docker compose -f docker-compose.companion.yml up -d
```

Allow TCP ports `11470` and `12470` through the computer firewall. In the PS4
application, open **Settings → Companion Server** and enter the computer's LAN
address, for example `192.168.1.50:11470`. Keep the companion available while
resolving and streaming torrent-backed sources.

## Controls

### Catalog and navigation

- **D-pad / left analog stick:** navigate menus, catalogs, and lists.
- **Left at the beginning of a catalog:** open the sidebar.
- **Right / Cross in the sidebar:** close it and return to content.
- **L1 / R1:** change the active main section.
- **Cross:** open the selected title, episode, setting, or stream.
- **Circle:** return to the previous screen.
- **Triangle:** reload supported catalog/search data.

### Player

- **Cross:** pause or resume.
- **Triangle:** restart.
- **Square:** show or hide the player HUD.
- **Up:** switch between fit and fill presentation.
- **L1 / R1:** move audio synchronization by −250/+250 ms.
- **Circle:** stop playback and return.

## Data and privacy

Account tokens, cached metadata, artwork, stream results, and settings are
stored in the application's private `/data` directory. Tokens must never be
committed or included in diagnostic reports. Addon responses and media URLs are
untrusted and are processed with bounded downloads and scheme checks.

## Test media and third-party material

The packaged Sintel clips are deterministic decoder tests. Sintel is copyright
Blender Foundation and distributed under Creative Commons Attribution 3.0.
The bundled Gontserrat font is distributed under the SIL Open Font License.
Stremio and PlayStation names and marks belong to their respective owners.

## Contributing

Keep changes focused, document hardware results, and never commit credentials,
copyrighted commercial media, Sony SDK files, generated PKGs, or private keys.
A useful report includes firmware, jailbreak version, package checksum, source
type, codec/container, visible behavior, FPS, and exact diagnostic error code.

## License

This repository is **source-available**, not open source under the OSI
definition. It uses the [PolyForm Noncommercial License 1.0.0](LICENSE).
Attribution and preservation of the required notice are mandatory. Commercial
use—including selling the software or incorporating it into a commercial
product or service—is not permitted without separate written permission from
Tahar Chtioui. For commercial licensing requests, contact
[@JavaScript-X](https://github.com/JavaScript-X).
