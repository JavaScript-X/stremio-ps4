# Stremio PS4

An experimental, community-built Stremio client for jailbroken PlayStation 4
consoles. The project is currently entering **M2: catalogs and metadata**.

> [!IMPORTANT]
> This is an unofficial project and is not affiliated with or endorsed by
> Stremio, Smart Code OOD, or Sony Interactive Entertainment. Use Stremio and
> its addons only with media you are authorized to access.

## Current status

The current hardware probe boots a native OpenOrbis application, initializes
1920x1080 video output, renders a controller-friendly shell, verifies HTTPS,
and can ask the PS4 AVPlayer to decode a packaged H.264 test clip. Packaging the
probe asset isolates hardware decoding from
the separate remote-stream transport path. Decoder textures use a dedicated
GPU-visible direct-memory arena, as required by the PS4 AVPlayer ABI.
Playback waits for AVPlayer's ready event before starting and uses its
non-blocking frame query so the controller and timeout remain responsive. The
video stream is enumerated and explicitly enabled before playback starts.
Extended frame metadata supplies the real NV12 pitch, and the renderer writes
rows directly into the framebuffer rather than calling a pixel function for
every output pixel.
Frame retrieval and NV12 conversion run on a worker thread so a blocking
decoder call cannot reduce the 60 FPS controller and presentation loop.
Shutdown joins that worker before stopping and closing AVPlayer, preventing a
decoder/close race.
Programmatic Home navigation is disabled because it is unstable on the tested
firmware. The native PS button performs Home/background navigation safely.
Version 1.21 adds the first Stremio addon-protocol catalog path: a bounded HTTPS
download of Cinemeta's top-movies catalog and dependency-free parsing of four
metadata previews. It does not log in, download posters, load user addons, or
present decoded audio yet.

M1 controller test controls:

- **Left/Right** moves the highlighted poster card.
- **Triangle** downloads and parses the Cinemeta top-movies catalog. Success
  reports the first real movie title and makes four metadata cards selectable.
- **Cross** reports the title of the focused Cinemeta item after the catalog has
  loaded.
- **Square** runs the AVPlayer hardware-decoder probe. Success reports the
  dimensions of the first decoded frame.
- **Circle** cancels an active AVPlayer probe.
- After a successful decode, the video plays continuously as a centered
  preview. **Cross** pauses/resumes and **Circle** stops and returns to the shell.
- During playback, **Options** stops video and returns to the app shell. From
  the shell it displays a reminder to use the native **PS button** to go Home.

| Milestone | Scope | Status |
| --- | --- | --- |
| M0 | PKG, video output, diagnostics | Validated on PS4 FW 13.02 |
| M1 | Controller, HTTPS, and local H.264 decode proof | Validated on PS4 FW 13.02 |
| M2 | Stremio login, catalogs, search and metadata | In progress |
| M3 | Direct stream selection and playback | Planned |
| M4 | Companion Stremio server integration | Planned |
| M5 | Library, progress sync, subtitles and settings | Planned |

See [docs/architecture.md](docs/architecture.md) for the intended design and
[docs/testing-m0.md](docs/testing-m0.md) for the first console test.

## Prerequisites

- A jailbroken PS4 capable of installing homebrew PKGs
- The [OpenOrbis PS4 Toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain)
- A Linux environment or WSL with LLVM 18 (`clang-18`, `clang++-18`,
  `ld.lld-18`) and `make`
- `OO_PS4_TOOLCHAIN` pointing to the OpenOrbis toolchain root

The build uses OpenOrbis' `_common/graphics.cpp` at compile time. No OpenOrbis
binaries or Sony SDK files are committed to this repository.

## Build

### Reproducible Docker build

Docker Desktop is the recommended build path on Windows. The image downloads
the official OpenOrbis v0.5.4 LLVM 18 archive and verifies its published
SHA-256 before extracting it.

```powershell
./scripts/fetch-openorbis.ps1
docker compose build builder
docker compose run --rm builder
docker compose run --rm packager
```

### Existing OpenOrbis installation

```sh
export OO_PS4_TOOLCHAIN=/path/to/OpenOrbis-PS4-Toolchain
make check
make
```

The builder produces the signed package metadata and executable. The packager
uses OpenOrbis' official compatibility image for its legacy SSL/ICU runtime.
The expected package is:

```text
dist/IV0000-BREW00100_00-STREMIOPS4000000.pkg
```

The package step needs the standard OpenOrbis runtime files at:

```text
$OO_PS4_TOOLCHAIN/samples/_common/sce_sys/about/right.sprx
$OO_PS4_TOOLCHAIN/samples/_common/sce_sys/icon0.png
```

`assets/sintel-trailer.mp4` is the normal-motion Sintel trailer mirrored by W3C
and is included only as a deterministic playback test. Sintel is copyright
Blender Foundation and distributed under Creative Commons Attribution 3.0.

Some OpenOrbis releases arrange these assets differently. If `make check`
reports a missing path, set `RIGHT_SPRX` or `ICON0` explicitly:

```sh
make RIGHT_SPRX=/path/to/right.sprx ICON0=/path/to/icon0.png
```

## Contributing

Keep changes focused and never commit credentials, copyrighted media, official
Sony SDK material, or generated PKGs. Test reports should include the firmware,
payload/loader, visible result, and the final diagnostic lines.
