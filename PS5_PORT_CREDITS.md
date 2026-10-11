# PS5-to-PS4 port credits

This branch ports the application structure and interface design of
[Sp9nky/unofficial-stremio-ps5-port](https://github.com/Sp9nky/unofficial-stremio-ps5-port)
from PlayStation 5 to PlayStation 4/OpenOrbis.

## Upstream project

- **Sp9nky** — creator and primary contributor of the unofficial Stremio PS5
  port. GitHub: [@Sp9nky](https://github.com/Sp9nky)
- All contributors whose work is recorded in the upstream repository history.
- The upstream project is licensed under GNU GPL v3.0.

The PS4 adaptation uses the upstream information architecture and interaction
model: Board, Discover, Library, Addons and Settings; focus-controlled rail;
hero content coupled to the selected title; catalog rows and poster grids;
details, episodes and stream group concepts; search through the console IME;
semantic Stremio theme tokens; and state-driven rendering.

The repository includes a source snapshot of the upstream project at commit
`286445df3e01a7f8d40d7c757b84e86980bdd0a5` in
the preserved upstream source tree. Portable upstream assets are included in
the PS4 package. Their original license and notice files remain alongside them.

## Important upstream acknowledgements

The PS5 project itself credits BlackBearReloaded and `ps5-homebrew-ui`, the
PS5 payload SDK and PacBrew libraries, Kodi PS5 research, Nuvio PS5, dht,
miniupnpc, stb, nlohmann/json, libwebp, the Inter/Montserrat/DejaVu/Noto font
projects, PSBrew tools, ShadowMountPlus, etaHEN, kstuff and Stremio. Consult
the upstream repository's `THIRD_PARTY.md` for the complete authoritative
list and license details.

No Sony SDK, firmware, keys, or proprietary Sony interface code is included.
The PS5-specific runtime source is retained for GPL attribution and
porting reference; it is not compiled. PS5-only `sceAgc`, OpenGL and `.ffpfsc`
runtime paths are replaced in the executable by PS4 OpenOrbis services and PKG
packaging.
