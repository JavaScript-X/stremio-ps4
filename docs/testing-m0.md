# M0 console test

## Validated result

M0 was validated on a jailbroken PS4 running firmware 13.02 with HEN. The
package installed successfully, initialized the active controller, presented
stable native 1920x1080 double-buffered output, remained responsive, and
returned to the home screen with the Options button without crashing.

Validated package version: `1.02`

Validated source commit: `a2654f8`

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
