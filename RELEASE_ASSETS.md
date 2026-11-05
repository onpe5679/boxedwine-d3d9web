# RELEASE_ASSETS - binaries: what comes later, and what has to be done first

## 0. Release plan

- **Release 1 (this one) is source only.** No `.wasm`/`.js` runtime, no guest filesystem zip, no JIT profile, no prebuilt `d3d9.dll` is attached.
- **A later GitHub Release** will carry binaries, built from a tag of this repository, after the conditions in section 1 are met. Prebuilt binaries belong on a Release page, not in git history: they are large, change on every build and carry their own licence duties.

Tags: **VERIFIED** (checked directly on the files), **INFERRED**, **NOT VERIFIED**. All hashes and sizes were measured on the maintainer's files on 2026-11-04/05.

## 1. Conditions before the first binary Release

1. **Generic rebuild of the runtime** with the generic target (`...Touch2MultiDirectLoops`, see `README.md`), not a re-upload of the deployed build. Reasons: the deployed build carries the Azrok-specific code and about 100 `AZROK_*` log strings, and it embeds a home path (below).
2. **Home-path prefix map.** Build with `-ffile-prefix-map=<absolute project directory>=.` added to the compile flags (for example through `EXTRA_CPP_FLAGS`; INFERRED to work with Emscripten's clang), then byte-scan the result. Do the same when rebuilding Wine. The scan must find no user directory, no drive-letter path and no private host name.
3. **`gulim.ttc` removed** from the guest filesystem (a Windows font, see C below), replaced by a freely licensed Korean font or left for users to add themselves.
4. **Wine LGPL patch and source offer prepared:** `d3d9web/wine/wine-11.0-changes.patch` checked against the Wine that is actually shipped, plus a link to the pristine `wine-11.0.tar.xz` and a written offer.
5. `NOTICE`/`THIRD_PARTY_NOTICES.md`, `LICENSE` and a `SOURCES.txt` (package, version, source URL for every GPL/LGPL package in the guest filesystem) attached to the Release.
6. Record the SHA-256 of every asset in the release notes and name the tag it was built from.
7. Run the byte-scan again on every asset (and on each entry of the guest-fs zip) before upload.

## 2. Planned assets and their state today

| # | Asset | Release 1 | Later Release | Size | SHA-256 (maintainer's current file) | State |
|---|---|---|---|---:|---|---|
| A | Web runtime `boxedwine.<stem>.js` + `.wasm` (+ generated `.html`) | not attached | **yes, after a generic rebuild** | deployed build: JS 338,487 B, Wasm 3,171,343 B | JS `35d072b873077702ca2d2d09bfdb78b5b1c8b9608b85d40f67520f4e78cc8be3`, Wasm `02b95215e12232eef772a54c0775cd057b5ea833356862082f10e11aba1ef268` | deployed build not publishable as is |
| B | Prebuilt guest `d3d9.dll` (production variant) | not attached | yes | 24,576 B | `2de7d20759a9e218c23c545a4a929c812afbdc09336c2a9897fc49cd487194d9` | clean |
| C | Guest filesystem zip (Tiny Core 15 + Wine 11.0 + Korean locale/XIM/audio/Winsock fixes) | not attached | **not as is** | 167,825,891 B | `ae1959c214a5bff5afe22e300e554525c586ae93d4e90af6545d14b89894d3af` | blocked (font, build path, LGPL source) |
| D | App zips (`apps/*.zip`) | never | never | ~157 MB each | - | contain the whole game data |
| E | JIT cache / profile zips (`profiles/*.zip`) | never | never | ~7 MB | - | derived from a recorded play session of one game |
| F | Other guest-fs variants (`...-batangquestfont*.zip`, diagnostics) | never | never | 184 MB etc. | - | at least one adds Windows Batang fonts (docstring of the build script) |

## A. Web runtime (`.js` / `.wasm` / `.html`)

- The files in `Build/FOTR10` are **byte-identical** to the deployed r10 production files (VERIFIED: hashed against the deployed copy and against the hashes recorded in the maintainer's deployment note).
- That build enables the Azrok-specific code (flags in `PATCHES.md`). The JS contains 103 `azrok` string matches and the wasm 18 (VERIFIED). It was tuned for one game. The public asset should come from the generic target; that target has **not been built from a clean checkout** (NOT VERIFIED).
- **Blocker (VERIFIED):** the deployed `.wasm` embeds one absolute source-file path under the maintainer's home directory (the string ends in `.../source/util/ring_...`; INFERRED to be a `__FILE__`-style diagnostic string). A prefix-map rebuild (condition 2) removes it; overwriting the NUL-terminated string in place with a shorter neutral one is an alternative (INFERRED to be safe because it is only used for diagnostics). Either changes the hash, so re-record it.
- Licence: Boxedwine is GPL-2.0, so the combined work is GPL-2.0. The wasm also contains code from `lib/zlib`, `lib/sdl2` (zlib licence), `lib/asmjit` (zlib licence), `lib/softfloat` (Berkeley SoftFloat 3 licence) and the Emscripten runtime (MIT/UIUC). Ship a `NOTICE` with these and link the tagged source.

## B. Prebuilt `d3d9.dll`

- Built from `d3d9web/guest/d3d9_web.cpp` with `build_d3d9.bat production`. VERIFIED: rebuilding the current source reproduces the deployed DLL's `.text`, `.data` and `.reloc` sections exactly; the only differences are the link timestamp (3 bytes in the debug directory) and the Rich header (a different MSVC minor version/time).
- The DLL contains no user path, no PDB path and no `azrok` string (VERIFIED by byte scan).
- Licence: GPL-2.0 (this repository). It is a 32-bit PE.

## C. Guest filesystem zip

Contents (VERIFIED from the central directory, 8,672 entries, 619.3 MB uncompressed): Wine 11.0 under `/opt/wine`, a Tiny Core Linux 15 userland under `/usr/local` and `/bin`, 1,782 `dep/*.tcz.dep` package-dependency files, and a Wine prefix under `/home/username/.wine`.

**Do not publish this zip as is. Three blockers:**

1. **Windows font.** `home/username/.wine/drive_c/windows/Fonts/gulim.ttc` (13,533,424 B, 4 faces, name table mentions Microsoft) is INFERRED to be the genuine Windows Gulim font, which is not redistributable. Remove it or replace it with a freely licensed Korean font (for example an SIL OFL one). Whether any behaviour depends on Gulim (font substitutes in the registry) is NOT VERIFIED.
2. **Build path (VERIFIED).** `opt/wine/lib/wine/i386-unix/ws2_32.dll.so` embeds the maintainer's home path and project folder name (the source path of the patched `dlls/ws2_32/socket.c`, a diagnostic string). Rebuild with a prefix map, or overwrite the string in place, before publishing.
3. **Modified LGPL code needs source (VERIFIED duty, partly verified content).** The Wine tree used differs from pristine `wine-11.0.tar.xz` in 15 files (+163 / -34 lines). The patch is `d3d9web/wine/wine-11.0-changes.patch`. Only for `ws2_32.dll.so` is it VERIFIED that the shipped binary was built from that tree; for the other 14 files it is NOT VERIFIED that the patch matches the shipped binaries exactly - rebuild, or at least diff, before release.

Also in the zip, to be named in the release notes (what they are is INFERRED from file names; versions from `version.txt` where present):

| Component | Where | Licence (check the shipped text) |
|---|---|---|
| Wine 11.0 (modified) | `opt/wine` | LGPL-2.1+ |
| Tiny Core Linux 15 userland, BusyBox, Samba config, OpenSSH, GnuTLS, GLib, GStreamer, mpg123, libogg/libvorbis/libtheora, libjpeg-turbo, libpng, p7zip | `/usr/local`, `/bin`, `/usr` | GPL / LGPL / BSD / zlib / mixed; licence files exist for several (`usr/local/share/doc/*`, `usr/local/share/licences/*`) |
| Liberation fonts, Font Awesome webfont | `usr/local/share/fonts` | SIL OFL 1.1 / see each font's licence (Liberation `LICENSE` is present) |
| DXVK 2.5.2 (`d3d8/d3d9/d3d10core/d3d11/dxgi.dll`) | `home/username/.wine/drive_c/dxvk` | zlib licence (upstream: github.com/doitsujin/dxvk) |
| `ddraw.dll` + `ddraw.ini` + GLSL shader pack | `home/username/.wine/drive_c/ddraw` | INFERRED to be cnc-ddraw (signature `ddraw.ini` + shader pack); licence NOT VERIFIED. Not needed by the d3d9 path of this repository - NOT VERIFIED that nothing loads it. Consider removing. |

Source offer: for GPL/LGPL components provide, in the release notes, either links to the exact source packages (Tiny Core: <http://tinycorelinux.net>; WineHQ source tarball `wine-11.0.tar.xz` from <https://dl.winehq.org/wine/source/> - links not checked by me) or a written offer valid for three years. Keep a `SOURCES.txt` with package name, version and URL for every GPL/LGPL package; the package versions were not enumerated here (NOT VERIFIED).

Byte-scan note: the zip also matched my private-LAN-address and private-key-marker patterns in standard Tiny Core files (sample `smb.conf`, `openssl.cnf`, GnuTLS/OpenSSH binaries); these are example strings and PEM markers, not secrets (INFERRED from the file names; I did not open each).

## D. Never publish

- `apps/*.zip` - contain the original game's full data (`Data`, `.wmb`, `.mdl`, `.pcx`, `.wis`, `.tga`, `.ogg`, `.mp3`, `.wmv`, ...).
- `profiles/*.zip` - JIT caches recorded from one game's code and play session.
- Any guest-fs variant that adds Windows fonts (`...batangquestfont*`).
- Logs, dumps, captures, browser profiles, server state, deployment manifests, tokens.

## E. Pre-upload scan

Byte-regex scan of each asset (and of each entry of the guest-fs zip, in memory) for: Windows user-profile paths (a drive letter followed by `Users`), the same paths as seen from WSL (the c-drive mount), Linux home directories, Tailscale CGNAT addresses (the 100.64 to 100.127 block), tailnet host names, private LAN addresses (the 192.168 block), hostnames of private servers, `BEGIN ... PRIVATE KEY`, tokens. That is how the findings above were produced; no scan script is part of this repository.
