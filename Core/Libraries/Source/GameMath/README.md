# GameMath

Deterministic math for game logic (E1-49, ADR-0003 §2.2). The sources under
`musl/` are **libm from musl**, unchanged, including their original copyright
headers.

| | |
|---|---|
| Upstream | musl libc |
| Release | 1.2.5 |
| Git commit | `0784374d561435f7c787a555aeab8ede699ed298` (tag `v1.2.5` on git.musl-libc.org) |
| Tarball | https://musl.libc.org/releases/musl-1.2.5.tar.gz |
| SHA-256 | `a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4` |
| License | MIT. Full text: [`LICENSES/musl-MIT.txt`](../../../../LICENSES/musl-MIT.txt) and [`musl/COPYRIGHT`](musl/COPYRIGHT) |

musl grew out of fdlibm / FreeBSD msun. Individual `.c` files keep the SunPro
notice they shipped with. MIT is compatible with GPLv3.

## What is compiled

Double: `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `pow`.
Float: `sinf`, `cosf`, `tanf`, `asinf`, `acosf`, `atanf`, `atan2f`.

Not in this set: `exp` / `log` / `fmod` and the `*f` forms (no callers in
`GameLogic`), `powf` (only `WWMath/vp.cpp`, not logic), and `sqrt` / `floor` /
`ceil` / `trunc` (platform libm; IEEE 754 makes them exact). Long-double
entry points (`*l.c`) are not compiled. `musl/src/internal/libm.h` still
contains musl's `long double` format check; that header is upstream.

`sqrt` inside `asin` / `acos` stays the platform `sqrt`.

`musl_rename.h` renames the public symbols (`sin` → `gm_musl_sin`, …) so
GameMath does not replace libc, and `#undef`s `__FP_FAST_FMA` so AArch64 and
x86_64 take the same non-fma source path. `hidden` matches musl
`src/include/features.h`:

```c
#define hidden __attribute__((__visibility__("hidden")))
```

That musl header is not vendored: it `#include`s musl's public `features.h`,
which must not sit ahead of the system headers.

## Build

The musl target is `gamemath_lib.cmake` (`include()`, not
`add_subdirectory` of this directory). `CMakeLists.txt` here belongs to the
fp gate. `cmake/gamemath.cmake` includes the fragment only when
`SAGE_USE_DETERMINISTIC_MATH` is ON, and that is when the game links it.

## Flag

`SAGE_USE_DETERMINISTIC_MATH` stays **OFF** until `tests/gamemath` reports a
passing bit-exact run of x86_64 against arm64 (`qemu-aarch64`). A skip is not
a pass. `USE_DETERMINISTIC_MATH` is defined only when the option is ON.

`GameMath::floorToInt32` / `ceilToInt32` / `truncToInt32` / `roundToInt32`
live in `Core/Libraries/Include/Lib/GameMathInt.h`, which is on the include
path while the flag is off. They are the ordinary conversions for new BFME
code. `REAL_TO_INT_FLOOR` / `REAL_TO_INT_CEIL` are a different function and
must not be edited (ADR-0003 §2.7).
