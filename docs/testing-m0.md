# M0 console test

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

