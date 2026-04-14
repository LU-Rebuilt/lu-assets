# lu-assets

C++20 library for parsing LEGO Universe client file formats. Provides complete, cross-platform parsers for all major asset types used by the original client.

Part of the [LU-Rebuilt](https://github.com/LU-Rebuilt) project.

> **Note:** This project was developed with significant AI assistance (Claude by Anthropic). All code has been reviewed and validated by the project maintainer, but AI-generated code may contain subtle issues. Contributions and reviews are welcome.

## Supported Formats

| Category | Formats |
|----------|---------|
| **NetDevil Archive** | PK archives, SD0 compression |
| **NetDevil Database** | FDB (flat database, converts to SQLite) |
| **NetDevil Zones** | LUZ, LVL, terrain (.raw), LU triggers, AUD, ZAL, AST, SCM |
| **NetDevil Common** | LDF (key-value config) |
| **Gamebryo Engine** | NIF/KF meshes & animation, KFM animation manager, .settings |
| **Havok Physics** | HKX binary & XML packfiles (rigid bodies, shapes, scene data) |
| **LEGO** | LXFML brick models, .g geometry, brick assembly & colors |
| **Microsoft** | DDS textures, TGA images, FXO shaders |
| **FMOD Audio** | FEV events, FSB sound banks |
| **ForkParticle** | PSB particle systems |
| **Scaleform** | GFX UI files |

## Building

### Requirements

- CMake 3.25+
- C++20 compiler (GCC 12+, Clang 15+, MSVC 2022+)
- zlib
- SQLite3

### Build

```bash
cmake -B build
cmake --build build -j$(nproc)
```

### Run Tests

```bash
ctest --test-dir build
```

## Usage as a Dependency

### Via FetchContent (recommended)

```cmake
include(FetchContent)
FetchContent_Declare(lu_assets
    GIT_REPOSITORY https://github.com/LU-Rebuilt/lu-assets.git
    GIT_TAG v0.1.0
)
FetchContent_MakeAvailable(lu_assets)

target_link_libraries(your_target PRIVATE lu_assets::lu_assets)
```

For local development, override the fetch with a local checkout:

```bash
cmake -B build -DFETCHCONTENT_SOURCE_DIR_LU_ASSETS=/path/to/local/lu-assets
```

### Via find_package (after install)

```bash
cmake --install build --prefix /usr/local
```

```cmake
find_package(lu_assets REQUIRED)
target_link_libraries(your_target PRIVATE lu_assets::lu_assets)
```

## Include Paths

Headers follow the module structure:

```cpp
#include "netdevil/archive/pk/pk_reader.h"    // or pk_types.h for data structures only
#include "netdevil/database/fdb/fdb_reader.h"
#include "gamebryo/nif/nif_reader.h"
#include "havok/reader/hkx_reader.h"          // or havok/types/hkx_types.h
#include "lego/lxfml/lxfml_reader.h"
#include "microsoft/dds/dds_reader.h"
#include "fmod/fev/fev_reader.h"
```

## Acknowledgments

This library is built from these authoritative sources:

- **Ghidra reverse engineering** of the original LEGO Universe client binary
- **[lcdr/lu_formats](https://github.com/lcdr/lu_formats)** — Kaitai Struct format definitions
- **[lcdr/utils](https://github.com/lcdr/utils)** — Python reference implementations
- **[DarkflameServer](https://github.com/DarkflameServer/DarkflameServer)** — Server emulator, compatibility target
- **[LUDevNet/Docs](https://github.com/LUDevNet/Docs)** — Community protocol documentation
- **[nif.xml](https://github.com/niftools/nifxml)** / **[NifSkope](https://github.com/niftools/nifskope)** — Gamebryo NIF format definitions
- **[HKXDocs](https://github.com/SimonNitzsche/HKXDocs)** — HKX binary format documentation
- **[lu-toolbox](https://github.com/Squareville/lu-toolbox)** — Blender LXFML reference
- **[NexusDashboard](https://github.com/DarkflameUniverse/NexusDashboard)** — LXFML rendering reference
- **[Assembly](https://github.com/LUDevNet/Assembly)** — Rust LU format library
- **[FFDec](https://github.com/jindrapetrik/jpexs-decompiler)** — Flash/SWF decompiler, reference for GFX/SWF format parsing

This project exists because of the LEGO Universe community. For over 15 years, developers, reverse engineers, and fans have kept this game alive through server emulators, format documentation, and preservation efforts. This library builds on that collective work.

## License

[GNU Affero General Public License v3.0](https://www.gnu.org/licenses/agpl-3.0.html) (AGPLv3)

LEGO Universe is a trademark of the LEGO Group. This project is not affiliated with or endorsed by the LEGO Group, NetDevil, or Gazillion Entertainment.
