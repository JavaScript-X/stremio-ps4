# Architecture

## Direction

Stremio PS4 is a native, controller-first client rather than a port of the Qt
desktop shell. The native client owns presentation, input, persistence, and
playback. Stremio's public HTTP services and addon protocol provide catalogs,
metadata, streams, subtitles, library state, and progress synchronization.

```text
PS4 native application
  UI and controller navigation
  Stremio API/addon client
  Playback adapter
        |
        +--- direct HTTP/HLS media
        |
        +--- LAN companion service
                torrent resolution
                transcoding/remuxing
                HTTP media delivery
```

## Why a companion service comes first

Many addon results are not immediately playable by a console media pipeline.
Running the official Stremio server on a PC or NAS lets the first useful PS4
release consume ordinary HTTP media while torrent handling, remuxing, and
transcoding remain off-console. A later milestone can investigate porting more
of the service once native playback is stable.

## Modules

- `platform`: PS4 video, audio, controller, clock, storage, and networking
- `ui`: focus model, views, reusable TV components, and accessibility
- `stremio`: authentication, API models, addon transport, and synchronization
- `player`: media capability inspection, direct playback, and server fallback
- `persistence`: settings, cached images, tokens, and playback recovery
- `diagnostics`: structured logs and a user-visible support report

Platform-specific code must remain behind narrow interfaces so protocol and UI
logic can be tested on a development computer without a PS4.

## Security boundaries

- Credentials and access tokens must never be written to logs.
- HTTPS certificate verification must not be disabled in release builds.
- Addon data and URLs are untrusted input and require size and scheme checks.
- The application must not execute addon-provided code.
- Local companion-service access should be explicitly configured or paired.

