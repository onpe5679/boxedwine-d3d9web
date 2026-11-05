# Third-party notices

This repository as a whole is GPL-2.0 (see `LICENSE`). It bundles third-party code and data under other licences.

- **Section 1** lists the one file the `d3d9web/` work added from outside (JSZip).
- **Section 2** lists the components that **upstream Boxedwine ships too**: everything under `lib/`, `tools/`, `source/`, `platform/` and `project/emscripten/binaryen_js.js` below came with upstream Boxedwine at commit `35dc3f0` and is kept here unchanged. They are listed so that their licences travel with the repository.
- Licence texts: each component keeps its own licence file or header (paths below). For Apache-2.0 the full text is in `licenses/Apache-2.0.txt` (an unmodified copy of the standard text, taken from the `packaging` project's `LICENSE.APACHE`, SHA-256 `0d542e0c8804e39aa7f37eb00da5a762149dc682d7829451287e11b938e94594`). The SIL Open Font License 1.1 text is **not** stored in the tree; the fonts' own name tables point to <http://scripts.sil.org/OFL> (add the text before a binary Release).

Tags: **VERIFIED** (read from the file itself), **INFERRED**, **NOT VERIFIED**. Licences were identified from the text or headers in the files, not by legal review.

## 1. Added for the sample page

### JSZip - `d3d9web/shell/assets/jszip.min.js`

| | |
|---|---|
| Version | not stated in the file (VERIFIED: no version string; the header says 2009-2014) |
| Copyright | (c) 2009-2014 Stuart Knightley, as written in the file header |
| Licence | "Dual licenced under the MIT license or GPLv3" (VERIFIED, header). Redistributed here under the **MIT** option |
| Bundled code | pako, "released under the MIT license" (VERIFIED, header); its copyright holders are not named in the file |
| Upstream | <http://stuartk.com/jszip>, <https://github.com/Stuk/jszip> (from the header); pako <https://github.com/nodeca/pako> |
| File | verbatim copy, 76,985 bytes, SHA-256 `9e170a21bb67ccf7dcab122c726e72637105a45d893350afa9b7a38369a0c1fd` |

MIT licence text that applies to this file:

```
The MIT License (MIT)

Copyright (c) 2009-2014 Stuart Knightley

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

The upstream `LICENSE.markdown` linked from the header may name further contributors; it was not fetched (NOT VERIFIED). Add them here if it does.

### BrowserFS - `browserfs.boxedwine.js` (NOT shipped)

An earlier sample page loaded `assets/browserfs.boxedwine.js` (845,392 bytes, SHA-256 `edfbf347e357e70ce4cbf22020c8df09a75375fda0b9204e860b87414b1f6995` on the maintainer's machine). It is **not included**: the file has no licence header and no version string (VERIFIED); it is a webpack bundle of BrowserFS (<https://github.com/jvilk/BrowserFS>) whose Boxedwine-specific origin is not recorded; the bundle embeds MIT code by Joyent/Node, the `buffer` package and pako, but a licence for the bundle itself cannot be confirmed from the file (BrowserFS is believed to be MIT, INFERRED). Neither this fork's `boxedwine-shell.js` nor upstream's `shell.html` references it (VERIFIED by search), and the sample page no longer loads it.

## 2. Components bundled with upstream Boxedwine (kept unchanged)

### 2.1 Web build tooling

| Component | Path | Version | Licence | Upstream |
|---|---|---|---|---|
| Binaryen (JS/Wasm build, used by the AOT cache pipeline) | `project/emscripten/binaryen_js.js` (14,301,313 B) | not visible in the file | Apache-2.0 - header: "@license Copyright 2026 The Binaryen Authors. SPDX-License-Identifier: Apache-2.0" (VERIFIED) | <https://github.com/WebAssembly/binaryen> |

### 2.2 Fonts

| Font | Path | Version / copyright (from the file's name table) | Licence | Upstream |
|---|---|---|---|---|
| Font Awesome (webfont) | `tools/debianFileSystem/fs/usr/share/fonts/truetype/fontawesome-webfont.ttf` | Version 4.6.3 2016; "Copyright Dave Gandy 2016"; vendor Fort Awesome (VERIFIED) | The name table gives only the licence URL <http://fontawesome.io/license/> and no licence text (VERIFIED). Font Awesome 4.x fonts are SIL OFL 1.1 (INFERRED, not stated in the file) | <http://fontawesome.io>, <https://fontawesome.com/v4/license/> |
| Cousine Regular | `lib/imgui/misc/fonts/Cousine-Regular.ttf` | Version 1.00; "Digitized data copyright (c) 2010 Google Corporation" | SIL OFL 1.1 (name table and `lib/imgui/docs/FONTS.txt`, VERIFIED) | <https://fonts.google.com/specimen/Cousine> |
| Droid Sans | `lib/imgui/misc/fonts/DroidSans.ttf` | Version 1.00; "Digitized data copyright (c) 2007, Google Corporation"; "Copyright (c) Steve Matteson" (FONTS.txt) | Apache-2.0 (name table and FONTS.txt, VERIFIED) | <https://www.fontsquirrel.com/fonts/droid-sans> |
| Karla Regular | `lib/imgui/misc/fonts/Karla-Regular.ttf` | Version 1.000; "Copyright (c) 2011, Jonathan Pinhorn ..., with Reserved Font Names Karla and Karla Tamil" (name table; FONTS.txt says 2012) | SIL OFL 1.1 (name table and FONTS.txt, VERIFIED) | <https://fonts.google.com/specimen/Karla> (INFERRED URL pattern) |
| ProggyClean | `lib/imgui/misc/fonts/ProggyClean.ttf` | 2004/04/15; "by Tristan Grimmer" (name table) | MIT, "Copyright (c) 2004, 2005 Tristan Grimmer" (FONTS.txt, VERIFIED; the TTF itself states no licence) | <http://www.proggyfonts.net/> |
| ProggyTiny | `lib/imgui/misc/fonts/ProggyTiny.ttf` | 2004/04/15; "by Tristan Grimmer" | MIT, same as above (FONTS.txt) | <http://www.proggyfonts.net/> |
| Roboto Medium | `lib/imgui/misc/fonts/Roboto-Medium.ttf` | Version 2.001152; 2014; "Copyright 2011 Google Inc." | Apache-2.0 (name table and FONTS.txt, VERIFIED) | <https://fonts.google.com/specimen/Roboto> |

`lib/imgui/imgui.cpp` also embeds a copy of ProggyClean (FONTS.txt). `lib/imgui/misc/fonts/` contains no separate licence files; the licences above come from the font name tables and `lib/imgui/docs/FONTS.txt`.

### 2.3 Graphics registries and headers

| Component | Path | Licence (from the file header) | Upstream |
|---|---|---|---|
| Vulkan API registry `vk.xml` | `lib/mesa/vkRegistry/vk.xml` | "Copyright 2015-2024 The Khronos Group Inc."; SPDX `Apache-2.0 OR MIT` (VERIFIED) | <https://github.com/KhronosGroup/Vulkan-Docs> |
| Mesa OSMesa interface `osmesa.h` | `lib/mesa/include/osmesa.h` | "Mesa 3-D graphics library", Copyright (C) 1999-2005 Brian Paul; MIT-style permission notice in the file (VERIFIED) | <https://www.mesa3d.org> |
| Vulkan headers (Boxedwine's own copy) | `source/vulkan/vk/*.h`, `source/vulkan/vk/vk_video/*.h`, `source/vulkan/vk_format_utils.h` | "Copyright 2014-2024 / 2015-2024 / 2023 The Khronos Group Inc."; SPDX `Apache-2.0` (VERIFIED) | <https://github.com/KhronosGroup/Vulkan-Headers> |
| Khronos headers inside SDL2 | `lib/sdl2/src/video/khronos/{vulkan,EGL,GLES2,KHR}/` | `vulkan.h`/`vulkan.hpp`: Copyright (c) 2015-2018 Khronos, Apache-2.0; `EGL/egl.h`: Copyright (c) 2013-2017 Khronos, MIT-style "Materials" permission notice (VERIFIED) | <https://github.com/KhronosGroup> |
| GL core header `glcorearb.h` | `lib/imgui/examples/libs/gl3w/GL/glcorearb.h` | Khronos MIT-style permission notice (VERIFIED, partial read) | <https://github.com/KhronosGroup/OpenGL-Registry> |

### 2.4 `lib/` libraries (each directory keeps its licence file)

| Library | Path / licence file | Version | Licence | Upstream |
|---|---|---|---|---|
| zlib | `lib/zlib/` - the licence is the header of `lib/zlib/zlib.h` (VERIFIED); `README` | **1.2.11, January 15th, 2017** (`ZLIB_VERSION`, VERIFIED); "Copyright (C) 1995-2017 Jean-loup Gailly and Mark Adler" | zlib licence (3-clause text in `zlib.h`, VERIFIED) | <https://zlib.net> |
| minizip (inside zlib) | `lib/zlib/contrib/minizip/` | "Copyright (C) 1998-2010 Gilles Vollant (minizip)" (header of `zip.h`) | stated per file header; follows zlib's terms (INFERRED, not re-read in full) | <http://www.winimage.com/zLibDll/minizip.html> |
| DotZLib (inside zlib) | `lib/zlib/contrib/dotzlib/LICENSE_1_0.txt` | - | Boost Software License 1.0 (VERIFIED) | <https://zlib.net> (contrib) |
| other `lib/zlib/contrib/*` | see `lib/zlib/contrib/README.contrib` and the file headers | - | per file | <https://zlib.net> |
| SDL 2 | `lib/sdl2/COPYING.txt`; `lib/sdl2/debian/copyright` | 2.0.x (not stated here) | zlib licence: "Copyright (C) 1997-2020 Sam Lantinga"; `debian/copyright` says "License: zlib/libpng" (VERIFIED) | <https://www.libsdl.org> |
| SDL 2 test programs | `lib/sdl2/test/COPYING` | - | public domain (VERIFIED) | <https://www.libsdl.org> |
| SDL 2 Xcode installer licence | `lib/sdl2/Xcode/SDL/pkg-support/resources/License.txt` | - | SDL licence text for the macOS package (not re-read) | <https://www.libsdl.org> |
| HIDAPI (inside SDL 2) | `lib/sdl2/src/hidapi/LICENSE.txt`, `LICENSE-bsd.txt`, `LICENSE-gpl3.txt`, `LICENSE-orig.txt` | - | "can be used under one of three licenses": GPL 3.0, BSD-style, or the original HIDAPI licence; Copyright (c) 2010 Alan Ott, Signal 11 Software (VERIFIED) | <https://github.com/libusb/hidapi> |
| yuv2rgb (inside SDL 2) | `lib/sdl2/src/video/yuv2rgb/LICENSE` | - | BSD-style (redistribution conditions); Copyright (c) 2016 Adrien Descamps (VERIFIED) | <https://github.com/descampsa/yuv2rgb> |
| libm (inside SDL 2) | `lib/sdl2/src/libm/` | - | Sun Microsystems permission notice, "Copyright (C) 1993 by Sun Microsystems" (VERIFIED in `e_atan2.c`) | fdlibm |
| bitmap fonts for the iOS demo | `lib/sdl2/Xcode-iOS/Demos/data/bitmapfont/license.txt` | font-pack.zip, 16 Jan 2008, by Marc Russell (spicypixel.net) | "free art assets to be used under the terms of this document" (VERIFIED; read the file for the exact terms) | <http://www.spicypixel.net> |
| SDL 1.2 | `lib/sdl1/COPYING.txt` | 1.2.x | GNU (Lesser) General Public License; desktop builds only | <https://www.libsdl.org> |
| Dear ImGui | `lib/imgui/LICENSE.txt` | - | MIT | <https://github.com/ocornut/imgui> |
| stb_truetype / stb_rect_pack / stb_textedit (as `imstb_*.h`) | `lib/imgui/imstb_truetype.h` (v1.20), `imstb_rectpack.h` (v1.00), `imstb_textedit.h` (v1.13) | as listed (VERIFIED) | dual "ALTERNATIVE A - MIT License / ALTERNATIVE B - Public Domain (www.unlicense.org)" (VERIFIED) | <https://github.com/nothings/stb> |
| GLFW | `lib/imgui/examples/libs/glfw/COPYING.txt` (+ headers) | - | zlib/libpng-style: Copyright (c) 2002-2006 Marcus Geelnard, 2006-2010 Camilla Berglund (VERIFIED) | <https://www.glfw.org> |
| gl3w | `lib/imgui/examples/libs/gl3w/` | - | no licence file; contains the Khronos `glcorearb.h` header (see 2.3); gl3w's own licence NOT VERIFIED | <https://github.com/skaslev/gl3w> |
| uSynergy | `lib/imgui/examples/libs/usynergy/` | 1.0.0, July 7th, 2012 | zlib-style; Copyright (C) 2012 Synergy Si Ltd., Copyright (c) 2012 Alex Evans (VERIFIED in header) | <https://github.com/symless/synergy-core> (named in its header) |
| imgui_freetype | `lib/imgui/misc/freetype/` | - | part of Dear ImGui (MIT); links FreeType when built | <https://github.com/ocornut/imgui> |
| AsmJit | `lib/asmjit/LICENSE.md`, `lib/asmjit/db/LICENSE.md` | - | zlib-style; Copyright (c) 2008-2025 The AsmJit Authors (VERIFIED) | <https://asmjit.com> |
| GLEW | `lib/glew/LICENSE.txt` | - | modified BSD with Mesa/Khronos MIT sections; Copyright (C) 2002-2007 Milan Ikits, Marcelo E. Magallon, 2002 Lev Povalahev | <http://glew.sourceforge.net> |
| pugixml | `lib/pugixml/LICENSE.md` | - | MIT | <https://pugixml.org> |
| RtMidi | `lib/rtmidi/LICENSE` | - | MIT-style | <http://www.music.mcgill.ca/> (the URL named in its README; the project page is INFERRED to be under `~gary/rtmidi/`) |
| SIMDe | `lib/simde/COPYING` | - | MIT | <https://github.com/simd-everywhere/simde> |
| Berkeley SoftFloat | `lib/softfloat/COPYING.txt` | Release 3e (2018 Jan 20) | Berkeley SoftFloat 3e licence (BSD-style), John R. Hauser (VERIFIED) | <http://www.jhauser.us/arithmetic/SoftFloat-3/doc/SoftFloat-source.html> (URL given in its README) |
| tiny-process-library | `lib/tiny-process/LICENSE` | - | MIT | <https://gitlab.com/eidheim/tiny-process-library> |

### 2.5 Vendored single files under `source/` and `platform/`

| Component | Path | Licence (from the file header) | Upstream |
|---|---|---|---|
| khash | `source/opengl/es/khash.h` | MIT; Copyright (c) 2008, 2009, 2011 Attractive Chaos | <https://github.com/attractivechaos/klib> (INFERRED; the header gives no URL) |
| imgui_markdown | `source/ui/utils/imgui_markdown.h` | zlib; Copyright (c) 2019 Juliette Foucaut & Doug Binks | <https://github.com/juliettef/imgui_markdown> |
| hash_table (as `bhashtable.h`) | `source/util/bhashtable.h` | "Licensed under the 2-clause BSD License"; taken from H2CO3/hash_table | <https://github.com/H2CO3/hash_table> |
| moodycamel::ConcurrentQueue | `source/util/concurrentqueue.h` | Simplified BSD; Copyright (c) 2013-2020 Cameron Desrochers | <http://moodycamel.com> (named in the header); repository <https://github.com/cameron314/concurrentqueue> INFERRED |
| pixelmatch (C++ port) | `source/util/pixelMatch.cpp` | ISC; Copyright (c) 2019 Mapbox | <https://github.com/mapbox/pixelmatch-cpp> |
| ring_buffer | `source/util/ring_buffer.{h,cpp}`, `ring_buffer.tcc.h` | Boost Software License 1.0 (SPDX `BSL-1.0`); Copyright Jean Pierre Cimalando 2018-2022 | <https://github.com/jpcima/ring-buffer> (INFERRED) |
| stb_image | `source/util/stb_image.h` | v2.25, public domain / see end of file | <https://github.com/nothings/stb> |
| atomic_ref | `platform/mac/atomic_ref.h`, `atomic_ref.gcc.h` | MIT | <https://github.com/avakar/atomic_ref> |
| Wine-derived CoreAudio/CoreMIDI/pixel-format code | `platform/mac/audiounit.cpp`, `coremidi.cpp`, `coremidi.h`, `pixelformat.cpp` | GNU LGPL 2.1 or later; Copyright Emmanuel Maillard 2005/2006, Alexandre Julliard 2012, Ken Thomases for CodeWeavers 2012/2013 (VERIFIED) | <https://www.winehq.org> |
| OSS API header | `source/kernel/devs/oss.h`, `tools/buildWine/oss/soundcard.h` | no licence text near the top of either file (NOT VERIFIED); `soundcard.h` describes itself as the OSS API header | <http://manuals.opensound.com/developer> |

### 2.6 `tools/`

| Content | Path | Licence |
|---|---|---|
| Wine patches used by upstream's Wine build scripts | `tools/buildWine/patches/*.patch` | patches to Wine; Wine is LGPL-2.1+ (the patches carry their authors' names) |
| Core Audio stub headers | `tools/coreaudio/` (9 files) | no licence header in the files read; origin NOT VERIFIED |
| Installer scripts, X11/OpenGL/Vulkan stubs and generators, Java helper tools | `tools/installer/`, `tools/x11/`, `tools/opengl/`, `tools/vulkan/`, `tools/BoxedWineRunner/`, `tools/debianFileSystem/src/` | Boxedwine project code (GPL-2.0) |
| Debian file-system skeleton | `tools/debianFileSystem/fs/` | holds only `changes.txt`, `version.txt`, timezone data and the Font Awesome file listed in 2.2 (the upstream-committed ELF libraries and jars were removed from this copy) |

The web build also links Emscripten's runtime and its SDL2/zlib ports (MIT/UIUC and the ports' own licences) when you build it yourself; they are not part of this repository.

Prebuilt binaries are not part of the first release. The licence duties for a future Release (Wine LGPL source offer, Tiny Core Linux packages, fonts) are in `RELEASE_ASSETS.md`.
