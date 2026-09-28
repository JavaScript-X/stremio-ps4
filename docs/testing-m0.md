# Real-hardware test guide

## Reference environment

- Console: PlayStation 4
- Firmware: 13.02
- Homebrew payload: GoldHEN
- Current package line: 4.x

Earlier milestone packages established installation, native VideoOut,
controller input, HTTPS, AVPlayer, AudioOut, and Videodec2 operation. New test
reports should use the complete checklist below instead of repeating only the
original M0 launch test.

## Before testing

1. Build from a clean checkout.
2. Record the Git commit and SHA-256 of the PKG.
3. Install the new package over the previous build.
4. Confirm the version shown under **Settings → About**.
5. Start the companion server when testing torrent-backed sources.

## Smoke test

1. Launch the application and leave it idle for 30 seconds.
2. Confirm the shell remains responsive and near 60 FPS.
3. Navigate every sidebar section with D-pad and left analog stick.
4. Open and close the sidebar; verify that the closing input is not repeated on
   the underlying catalog.
5. Move continuously across more than six posters in both directions.
6. Open details, return, background with the PS button, and resume.

## Network and account test

1. Link a Stremio account through the device-code screen.
2. Confirm synchronized addon count is nonzero.
3. Open Movies, Series, Public Domain, and Search.
4. Confirm cached artwork appears after revisiting a catalog.
5. Search for a movie and open its normal details and stream flow.

Never include an authentication token or private addon URL in a report.

## Stream and player test

1. Request streams and note lookup time and source count.
2. Record the selected source's container, codec, size, seeds, and peers.
3. Start playback and record buffering time.
4. Confirm video cadence and whether audio starts and remains synchronized.
5. Test pause/resume, restart, HUD toggle, fit/fill, and audio offset.
6. Stop playback, select a different source, and start playback again.
7. Reopen the same title and verify cached stream results appear immediately.
8. Stop with Circle and close from the PS4 system menu.

## Performance observations

Distinguish these measurements:

- Shell/input FPS: expected to remain at the display loop's 60 Hz.
- Source frame rate: commonly 24, 25, or 30 FPS for movies.
- Decoder FPS: should meet or exceed the source cadence.
- Transfer speed: depends on source peers, network, and companion storage.

A 24 FPS film is not expected to contain 60 unique video frames per second;
the UI and controller loop can still operate at 60 Hz.

## Report template

```text
Commit:
Package version:
PKG SHA-256:
PS4 model and firmware:
Payload and version:
Companion platform/version:
Connection: Ethernet/Wi-Fi
Screen or action tested:
Source container/codec/resolution/FPS:
Shell FPS:
Decoder FPS:
Audio result and sync:
Second-stream restart result:
Exact stage/error code:
Steps to reproduce:
Photo/video link (optional):
```

## Known diagnostic meaning

- AVPlayer stage 4: source rejected during `sceAvPlayerAddSource`; verify the
  URL, manifest, transport, and format.
- Videodec2 stage errors: record stage, native code, and submitted access-unit
  count.
- Companion/no-peer errors: retry another source before treating the player as
  broken.

Testing should use public-domain, freely licensed, or personally authorized
media only.
