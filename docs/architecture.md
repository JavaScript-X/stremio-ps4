# Architecture

## Design goals

Stremio PS4 is a native OpenOrbis application, not a Qt, Electron, Android, or
web-wrapper port. The design prioritizes controller navigation, predictable
memory use, bounded network input, hardware decoding, and recovery on real PS4
homebrew environments.

## Runtime overview

```text
┌──────────────────────────────── PS4 application ────────────────────────────┐
│                                                                            │
│  UI / focus model ── Catalog + metadata ── Account + addon transport       │
│         │                      │                         │                   │
│         └──────── Cache and private application data ───┘                   │
│                                │                                           │
│                    Playback session coordinator                            │
│                      │                    │                                │
│                 Videodec2             AVPlayer                             │
│                 H.264 video           HLS audio / compatibility tests      │
│                                                                            │
└───────────────────────────────────┬────────────────────────────────────────┘
                                    │ HTTP on the local network
                                    v
                    Official Stremio companion server
                     torrent resolution and HLS delivery
```

## Major components

### Native shell

`src/main.cpp` owns application state, DualShock input, view transitions,
asynchronous jobs, persistent settings, and coordination between browsing and
playback. Static views are rendered into both framebuffers only when state
changes; idle frames reuse those buffers to keep input and presentation at
60 Hz.

### Rendering and artwork

`src/graphics.*` provides the small immediate-mode 2D renderer. `src/poster.*`
decodes and prepares posters, wallpapers, and title logos. Catalog artwork is
cached under `/data`, and hero images are prepared off the render thread at
their display size to avoid expensive scaling during navigation animations.

### Stremio resources

`src/catalog.*` parses bounded catalog, metadata, episode, and stream responses.
Authentication uses Stremio's device-link flow. Synchronized addon URLs remain
remote services: addon code is never installed or executed on the console.

### Playback

- `src/videodec2.*` submits Annex-B H.264 access units to PS4 Videodec2 and
  exposes decoded frames for the native framebuffer.
- `src/fmp4_stream.*` converts the companion's fragmented-MP4 video segments
  into the Annex-B format expected by Videodec2 while preserving timing.
- `src/avplayer.*` supports Sony AVPlayer diagnostics and the companion's HLS
  audio rendition.
- `src/mp4_demux.*` extracts supported AVC tracks from complete cached MP4
  files.

The segmented player alternates bounded cache slots and prepares the next
fragment while the current fragment plays. Session teardown owns every worker,
decoder, audio handle, and preview surface so another source can start cleanly.

## Persistence

Private application storage contains:

- Stremio authentication and synchronized addon configuration.
- UI/navigation settings and companion address.
- Posters, wallpapers, title logos, metadata, and stream-result caches.
- Bounded media and elementary-stream cache files.

Secrets must never be written to logs or committed to the repository.

## Trust boundaries

- Catalogs, addon responses, URLs, playlists, images, and media are untrusted.
- Remote reads must enforce scheme, response-size, and timeout limits.
- Release HTTPS requests must retain certificate verification.
- The client must never execute addon-provided code.
- The companion address is explicitly configured by the user.
- Commercial or protected media is outside the project's test fixtures.

## Performance rules

- Network access, JPEG/PNG decoding, and media preparation stay off the render
  thread.
- Static screens redraw only after state changes.
- Images are decoded once at their intended presentation dimensions.
- Both PS4 framebuffers receive each new video frame to prevent stale-frame
  alternation while input and flips continue at 60 Hz.
- Playback stops catalog prefetch workers that would compete for CPU, network,
  or Sony service resources.
