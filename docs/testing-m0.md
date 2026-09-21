# M0 console test

## Validated result

M0 was validated on a jailbroken PS4 running firmware 13.02 with HEN. The
package installed successfully, initialized the active controller, presented
stable native 1920x1080 double-buffered output, remained responsive, and
returned to the home screen with the Options button without crashing.

Validated package version: `1.02`

Validated source commit: `a2654f8`

## M1 follow-up validation

Package version `1.04` was validated on the same firmware 13.02/HEN console:

- Left/Right moved focus across all four cards.
- Cross activated the focused card and displayed confirmation.
- Triangle completed a certificate-verified request to `www.stremio.com` with
  HTTP status 200.
- Options returned to the PS4 home screen.
- No crashes were observed.

## Goal

Prove that the package launches, allocates two 1080p framebuffers, and presents
frames correctly on the target console before adding input and networking.

## Procedure

1. Build the PKG from a clean checkout.
2. Install it through the PS4 Debug Settings package installer.
3. Launch **Stremio PS4** from the home screen.
4. Wait at least 30 seconds and observe whether the image remains stable.
5. Close the application using the PS button.

## Expected result

The display shows a dark background, left navigation rail, purple header bars,
and four gray poster placeholders. There should be no flickering or system
error dialog.

## Report template

```text
Firmware: 13.02
Jailbreak/payload:
Loader version:
PKG checksum:
Launch result:
Visible output:
Stable for 30 seconds: yes/no
How the app was closed:
Diagnostic output:
Photo/video link (optional):
```

## M1 AVPlayer validation

Package version `1.10` was validated on the firmware 13.02 console. Pressing
Square opened the packaged Mozilla CC0 MP4, completed AVPlayer's ready-event
handshake, enabled the video stream, and retrieved a decoded H.264 frame. The
reported output was `decoded frame 960x540` and appeared almost immediately.

This validates package file access, MP4 demuxing, H.264 hardware decoding,
GPU-visible decoder allocation, and decoded-frame retrieval. Displaying those
NV12 frames and consuming decoded audio remain separate follow-up work.

Package version `1.11` additionally displayed a centered 480x270 flower image
converted from the decoded NV12 frame. Circle returned to the shell and Options
returned Home. Resuming that backgrounded instance exposed stale VideoOut state,
so version `1.12` changes Options to stop playback and exit cleanly after the
Home navigation request.
