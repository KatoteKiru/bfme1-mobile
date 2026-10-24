# stb

Third-party single-header image decoder used by the WW3D2 texture loader (E1-44): BFME1 art
archives keep many textures as JPEG/PNG, which the Targa/DDS paths cannot read.
`stb_image.h` is **unchanged** from upstream.

| | |
|---|---|
| Upstream | [nothings/stb](https://github.com/nothings/stb) |
| File | `stb_image.h` |
| Version | v2.30 |
| SHA-256 | `594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3` |
| License | MIT (dual-licensed MIT / public domain; MIT chosen). Full text: [`LICENSES/stb-MIT.txt`](../../LICENSES/stb-MIT.txt) and the end of `stb_image.h` |

## How it is compiled

Exactly one translation unit defines `STB_IMAGE_IMPLEMENTATION`:
`Core/Libraries/Source/WWVegas/WW3D2/bfmeimagedecode.cpp`, with `STB_IMAGE_STATIC`
(no exported `stbi_*` symbols), `STBI_ONLY_JPEG`, `STBI_ONLY_PNG` and `STBI_NO_STDIO`
(files are read through the engine file factory, so BIG archives work). `STBI_NEON` is set on
AArch64. The include path is added to `corei_ww3d2` in
`Core/Libraries/Source/WWVegas/WW3D2/CMakeLists.txt`.

The Generals Online client keeps its own copies of `stb_image_write.h` / `stb_image_resize.h`
under `GeneralsMD/Code/GameEngine/Include/GameNetwork/GeneralsOnline/Vendor/stb_image/`; they are
unrelated to this one.
