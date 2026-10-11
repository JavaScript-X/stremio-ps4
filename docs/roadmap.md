# Roadmap

This roadmap tracks capabilities rather than historical package versions.
Items marked validated were tested on a real PS4 running firmware 13.02.

## Platform foundation

- [x] Reproducible OpenOrbis Docker build and PKG packaging.
- [x] Native 1920×1080 double-buffered output.
- [x] DualShock 4 D-pad and analog navigation.
- [x] Certificate-verified HTTPS and bounded cache storage.
- [x] Stable native PS-button background/home behavior.
- [ ] Validate additional firmware and jailbreak combinations.

## Stremio browsing

- [x] Device-link account authentication.
- [x] Synchronized addon collection.
- [x] Movies, series, public domain, search, and metadata details.
- [x] Episode selection and addon stream lookup.
- [x] Persistent poster, wallpaper, logo, metadata, and stream caches.
- [x] Android TV-inspired hero layout and continuous horizontal catalog.
- [ ] Library and Continue Watching views.
- [ ] Account watch-state and progress synchronization.

## Playback

- [x] Direct Videodec2 H.264 hardware decoding.
- [x] Companion fMP4/HLS torrent streaming pipeline.
- [x] Companion audio through Sony AVPlayer.
- [x] Pause, restart, stop, fit/fill, HUD, and audio-sync controls.
- [x] Reusable playback-session teardown and second-stream startup.
- [ ] Audio-language selection.
- [ ] Subtitle download, selection, styling, and synchronization.
- [ ] Reliable seeking across segmented torrent streams.
- [ ] Broader MKV, HEVC, WebM, DASH, and unusual H.264 compatibility.
- [ ] Resume playback after suspension or application restart.

## Usability and release engineering

- [x] Native search keyboard and cached search results.
- [x] Settings, diagnostics, account, and About screens.
- [x] Source information including file size, seeds, and peers.
- [ ] Cache-management screen and configurable storage limits.
- [ ] Localization and accessibility review.
- [ ] Automated host-side parser/demux tests in CI.
- [ ] Signed release checksums and upgrade notes.
- [ ] Extended memory, thermal, and multi-hour playback testing.
