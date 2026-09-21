# Stremio PS4

An experimental, community-built Stremio client for jailbroken PlayStation 4
consoles. The project is currently at **M0: hardware proof**.

> [!IMPORTANT]
> This is an unofficial project and is not affiliated with or endorsed by
> Stremio, Smart Code OOD, or Sony Interactive Entertainment. Use Stremio and
> its addons only with media you are authorized to access.

## Current status

The first target is intentionally small. It boots a native OpenOrbis
application, initializes 1920x1080 video output, renders a controller-friendly
placeholder shell, and writes diagnostic messages. Press **Options** to return
to the PS4 home screen. It does not log in, load
addons, or play media yet.

| Milestone | Scope | Status |
| --- | --- | --- |
| M0 | PKG, video output, diagnostics | In progress |
| M1 | Controller input and HTTP/HLS playback proof | Planned |
| M2 | Stremio login, catalogs, search and metadata | Planned |
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

Some OpenOrbis releases arrange these assets differently. If `make check`
reports a missing path, set `RIGHT_SPRX` or `ICON0` explicitly:

```sh
make RIGHT_SPRX=/path/to/right.sprx ICON0=/path/to/icon0.png
```

## Contributing

Keep changes focused and never commit credentials, copyrighted media, official
Sony SDK material, or generated PKGs. Test reports should include the firmware,
payload/loader, visible result, and the final diagnostic lines.
