# Notices & Licensing

This program vendors two third-party components. **Read this before
redistributing**, especially publicly.

| Component | What it is | License / status |
|---|---|---|
| **ooz** (`src/ooz/*`) | Reverse-engineered Oodle *decompressor* | See "Oodle / ooz" below — https://github.com/powzix/ooz |
| **miniz** (`src/miniz.*`) | zlib-compatible (de)compression | MIT — see `MINIZ_LICENSE.txt` — https://github.com/richgel999/miniz |
| `src/main.cpp`, `src/palsave.*`, `build.bat` | This tool | Treat as public domain / MIT. |

## Oodle / ooz — the important caveat

Palworld's `PlM1` saves are compressed with **Oodle**, a **proprietary** codec
owned by Epic Games (RAD Game Tools). This project does **not** ship Oodle.

`src/ooz/` is **[ooz](https://github.com/powzix/ooz)**, an independent, clean-room
**reverse-engineered** *decompressor* for Oodle's Kraken/Mermaid/Selkie/Leviathan
formats. The legal status of redistributing a reverse-engineered implementation of
a proprietary codec is **genuinely uncertain**. Many community modding tools ship
ooz anyway; that is not legal advice. **If you redistribute this project (source or
the compiled exe, which statically includes ooz), you do so at your own discretion
and risk.**

### License-cautious alternative (no ooz)

Replace the ooz decode path in `src/palsave.cpp` (the `PlM` branch of `decodeSav`)
with a call to `OodleLZ_Decompress` from an official `oo2core_9_win64.dll` loaded
at runtime via `LoadLibrary`/`GetProcAddress`. That DLL ships with many Unreal
Engine games (the ooz README notes it's freely available with *Warframe* on Steam).
Then remove `src/ooz/` entirely. Users would supply their own DLL.

## No affiliation

Not affiliated with, endorsed by, or associated with Pocketpair (Palworld),
Epic Games, or RAD Game Tools. "Palworld" and "Oodle" are trademarks of their
respective owners. Use on your own save files, at your own risk.
