# Wine 11.0 changes used by the guest filesystem

`wine-11.0-changes.patch` is a unified diff from the pristine `wine-11.0.tar.xz` to the edited Wine tree that the maintainer's guest filesystem was built from. It touches 15 files (+163 / -34 lines); the list and probable purposes are in `../../PATCHES.md`.

- Wine is LGPL-2.1 or later. This patch is the corresponding source for the modified modules; see `../../RELEASE_ASSETS.md` for what is and is not verified about the match with the shipped binaries.
- Apply with `patch -p1` from the root of an unpacked `wine-11.0` tree.
- The hunks contain Korean-specific choices (a hard-coded code page 949 macro named `AZROK_INPUT_CODEPAGE` in the ANSI edit control path). Treat them as a reference for what had to change for Korean IME input under BoxedWine, not as upstream-quality patches.
