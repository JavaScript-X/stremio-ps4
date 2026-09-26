# Stremio PS4

An experimental, community-built Stremio client for jailbroken PlayStation 4
consoles. The project is currently entering **M2: catalogs and metadata**.

The installed PS4 application is titled **Stremio**. Its icon combines the
official Stremio symbol with a small JavaScript-X creator badge; this remains
an unofficial community client.

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
Version 2.70 adds a controller-driven Stremio catalog browser: bounded HTTPS
downloads of Cinemeta catalogs, dependency-free parsing of paginated
metadata previews, bounded poster downloads, and cached 310x410 JPEG rendering.
Metahub poster requests explicitly select JPEG so WebP-backed catalog entries
remain compatible with the small decoder used by the native client.
Movie titles and navigation labels are rasterized directly into the native
framebuffer, so the catalog no longer depends on notifications for identity.
An animated top bar exposes Movies, Series, Public Domain, Search, and Settings
through L1/R1. Catalogs load off the render thread and are cached per tab, so
navigation remains responsive while purple Stremio placeholders appear
immediately. The next row stays fixed and preloaded; pressing Down promotes it
with a short bottom-to-top slide instead of continuously animating every card.
Poster compositing uses contiguous scanline copies to keep menu rendering near
60 FPS while selected cards retain a soft rounded focus animation.
The left analog stick mirrors D-pad navigation. Search opens the native PS4
system keyboard after consuming the opening Cross press, preventing that input
from immediately closing the IME, and queries the canonical Cinemeta endpoint.
Routine navigation no longer produces system-notification spam, and the
unstable in-app exit command has been removed in favor of the PS4 menu. The interface now
uses the bundled OFL-licensed Gontserrat font with anti-aliased text plus the
official Stremio mark in the top bar. Settings contains local and cached-HTTPS
playback diagnostics plus query reset and build information.
Detail screens also show IMDb rating and genres. Series metadata exposes up to
256 episodes with Left/Right episode navigation and Cross selection.
The official Public Domain Movies addon is available as a third catalog. Its
stream resources are parsed into a native results screen. Torrent descriptors
and arbitrary direct HTTPS streams are identified as requiring the planned
companion/cache bridge instead of being passed to AVPlayer's unsupported URL
path. A separate legal remote Sintel probe downloads through the app's
certificate-verified HTTPS client into persistent app storage and then feeds
that local cache to AVPlayer. The first run downloads the 4.37 MB trailer and
later runs reuse it. Playback now includes a native progress bar, elapsed/total
clock, visible playing/paused state, and a live measured decoder-FPS counter.
Packaged 360p, 720p, 1080p, and original-resolution Sintel variants provide
repeatable hardware-decoder comparisons. The player supports five-second
back/forward seeking and restart, and displays source resolution plus decoded
frame count. Catalog, metadata, and encoded poster responses persist in
`/data`; uncached posters are published individually instead of holding the
whole catalog until every image finishes.
The 2.51 hotfix discovers generated video-only MP4 streams by valid dimensions
rather than relying on one stream-type value. Feathered navigation panels use
pre-blended bands and lower-row posters use cached 90% textures, removing the
per-pixel blending and scaling that reduced shell performance in 2.50.
Version 2.52 expands AVPlayer's direct-memory texture arena for HD reference
surfaces and uses Constrained Baseline, single-reference, no-B-frame 720p and
1080p probes. It also fixes progressive-poster ownership when revisiting a
cached tab and masks outgoing catalog rows below the navigation viewport.
Version 2.60 keeps full-HD hardware decoding while reducing only its CPU debug
preview to 640x360, moves pagination and poster work to a background prefetch,
and adds lightweight tab lift and footer-shimmer animation. Search reports the
console's native Enter-button assignment because Sony's system IME follows
that setting for Cross/Circle behavior. Version 2.61 isolates playback tests
from catalog download/JPEG workers, which otherwise competed with AVPlayer and
distorted every resolution's measured FPS. It restores Sony's standard Search
IME mode and leaves Cross, Square, Triangle, R2, and Circle entirely owned by
the system keyboard while it is open.
Version 2.62 removes per-frame AVPlayer source-dimension mutex queries, keeps
the previously validated 720p conversion path, and reduces only the 1080p
diagnostic surface to 480x270. The app now releases its pad handle before
opening Sony's IME and reacquires it afterward, giving the system keyboard
exclusive controller ownership for its standard button layout.
Version 2.63 keeps infinite-scroll poster prefetch suspended for the complete
AVPlayer lifetime, including its opening phase. This prevents the catalog's
network, JPEG, and cache worker from silently restarting while a local or
remote playback benchmark is running.
Version 2.64 gives every playback test a bounded approximately 320x180 CPU
diagnostic surface while AVPlayer continues decoding the full source. This
removes the resolution-dependent scalar NV12-to-RGB workload from decoder-FPS
comparisons: 360p, 480p, 720p, and 1080p now convert roughly the same pixels.
Version 2.70 moves every playback diagnostic into Settings submenus for player
mode and quality. Rendered RGB preview and Sony AVPlayer decode-only modes can
now be compared at 360p, 480p, 720p, 1080p, and cached HTTPS 480p. A dedicated
About screen credits Tahar Chtioui (`@JavaScript-X`), shows the bundled creator
avatar, test platform, and public project repository.
The playback pipeline keeps reusable conversion surfaces, transfers a preview
only when a new decoded frame exists, and gives AVPlayer a six-frame output
queue. These changes remove per-frame heap churn and redundant 60 Hz copies
while keeping the interface and controller loop at 60 FPS.
The PS4 release objects are compiled with `-O2`; this is essential for the
scalar NV12-to-RGB conversion loop to sustain the test video's native 24 FPS
on the console CPU. The build check prints the active release optimization so
an accidental unoptimized package is visible in CI and local build logs.
System termination now stops AVPlayer before joining its frame worker, then
closes the decoder, IME, controller, HTTP, SSL, network, and user services in a
single orderly path. Native-style colored PS button badges replace the old
text-only control legends in screen-corner footers.
Settings also provides **Exit Application Safely** as an explicit way to run
the same cleanup path before returning to the system menu.
It does not log in, load user addons, or present decoded audio yet.

Controller controls:

- **D-pad or left analog stick** moves through cards and vertical lists.
- The default Movies catalog loads automatically at startup.
- **R1** moves right and **L1** moves left through the five animated top tabs.
- **Up/Down** continuously scrolls catalog pages and fetches more at the end.
- **Triangle** explicitly reloads the active catalog and its posters.
- **Cross** opens the focused item's metadata detail screen.
- On Series details, **Left/Right** browses episodes and **Cross** selects the
  focused episode.
- **Circle** returns from details to the catalog.
- **Square** runs the AVPlayer hardware-decoder probe. Success reports the
  dimensions of the first decoded frame.
- On Search, **Cross** opens the PS4 system keyboard. Its Search button submits
  the query; **Triangle** repeats it and **Circle** clears or returns from results.
- The Settings tab contains the packaged and cached-HTTPS playback tests.
- On a Public Domain detail page, **Cross** resolves addon streams. The stream
  screen uses **Up/Down**, **Cross**, and **Circle**.
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
