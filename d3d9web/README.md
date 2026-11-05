# d3d9web

Everything specific to the Direct3D9 -> WebGL2 bridge. See the top-level `README.md` for the architecture diagram and the build steps.

| Directory | Contents | Notes |
|---|---|---|
| `guest/` | `d3d9_web.cpp`, `d3d9.def`, `build_d3d9.bat`, `smoke/` | 32-bit guest DLL. `build_d3d9.bat [variant]` builds `production` (default), `plain`, `guestbatch`, `recompose`, `colorwritetrace`, `slottrace`. |
| `guest/smoke/` | INT 0x9A hello-world (`phase0_*`, `phase3_*`), COM smoke (`d3d9_com_smoke*`), capability bootstrap client, Winsock smoke tests | `build_phase4.bat` builds the COM smoke; the other files have no build script (compile with `cl /MT /GS-`). |
| `web_backend/host/` | `d3d9web-runtime.js` (WebGL2 runtime), `pack-ffp.js`, `bitpack.js`, `std140.js`, `ffp-*.js` | The runtime that `prepareD3D9WebRuntime()` creates. |
| `web_backend/shaders/` | `d3d9_ffp.vert`, `d3d9_ffp.frag` | Fixed-function pipeline in GLSL. |
| `shell/` | `index_d3d9web_phase9.html` | Example page; needs third-party `assets/browserfs.boxedwine.js` and `assets/jszip.min.js` (not included). |
| `tools/d3d9_probe/` | `d3d9proxy.cpp`, `build.bat`, `README.md` | Windows-only API-surface logger (Korean README, written for the original game). Its build writes `d3d9_flushprobe.dll`; rename it to `d3d9.dll` beside the EXE. |
| `wine/` | `wine-11.0-changes.patch` | See `wine/README.md`. |
| `recipes/` | `build_tinycore_tcz_overlay.py`, `replace_zip_entry.py`, `replace_zip_members.py`, `wine_getacp_probe.c` | Helpers for building/patching guest filesystem zips. |

Known staleness: `web_backend/host/README.md`, `web_backend/shaders/PORTING_NOTES.md` and `UNIFORMS.md` were written on 2026-07-19 against an earlier renderer; the current `d3d9web-runtime.js` is about 2.5x larger. Read them for design intent, and trust the code (NOT VERIFIED which statements are still accurate).
