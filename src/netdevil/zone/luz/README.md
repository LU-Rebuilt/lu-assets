# LUZ (Zone) Format

**Extension:** `.luz`
**Used by:** The LEGO Universe client to define zone layout, including scenes, boundaries, transitions, and paths

## Overview

LUZ files define the top-level structure of a game zone: which scenes (LVL files) compose it, player spawn points, zone boundaries, inter-zone transitions, and all NPC/platform/spawner/racing/rail paths. All shipped client files are version 41.

## Binary Layout

```
Offset  Size  Type      Field
------  ----  ----      -----
0x00    4     u32       version (41 in shipped client)
+0x00   4     u32       file_revision (if version > 35)
+0x00   4     u32       world_id
+0x00   12    f32[3]    spawn_position (x, y, z)
+0x00   16    f32[4]    spawn_rotation (x, y, z, w quaternion)

--- Scenes ---
+0x00   4     u32       scene_count
  Per scene:
  +0x00  1    u8        filename_len
  +0x01  N    char[N]   filename — relative path to .lvl file
  +N     4    u32       scene_id
  +N+4   4    u32       scene_type — 0=General, 1=Audio
  +N+8   1    u8        name_len
  +N+9   M    char[M]   scene_name
  (version 33 only: Vec3 scene_position + f32 scene_radius + u8[3] color)

--- Boundaries ---
  Read after scenes: zone boundary lines for transitions.
  Per boundary:
  +0x00  12   f32[3]    normal
  +0x0C  12   f32[3]    point
  +0x18  2    u16       dest_map_id
  +0x1A  2    u16       dest_instance_id
  +0x1C  4    u32       dest_clone_id
  +0x20  4    u32       dest_scene_id
  +0x24  12   f32[3]    spawn_location

--- String fields ---
  u1_wstr raw_path       — terrain heightmap file path (.raw)
  u1_wstr zone_name      — display name (version > 30)
  u1_wstr zone_description (version > 30)

--- Transition Data (version >= 32) ---
  Per transition:
    2 points (version <= 33 or >= 39), 5 points (version 34-38)
    Per point:
    +0x00  4  u32       scene_id
    +0x04  4  u32       layer_id
    +0x08  12 f32[3]    position

--- Path Chunk (version >= 35) ---
  +0x00  4    u32       path_chunk_size
  Per path:
  +0x00  4    u32       path_version
  +0x04  var  u1_wstr   name
  +var   4    u32       path_type (see LuzPathType enum)
  +var   4    u32       flags
  +var   4    u32       behavior (0=Loop, 1=Bounce, 2=Once)
  ... type-specific path data and waypoints follow
```

### LuzPathType Enum

```
Value  Type
-----  ----
0      NPC — creature movement paths
1      MovingPlatform — elevators, bridges
2      Property — player property boundaries
3      Camera — cinematic spline paths
4      Spawner — object spawner nodes
5      Showcase — buildarea zones
6      Racing — track waypoints
7      Rail — Ninjago rail paths
```

## Key Details

- Little-endian byte order
- No magic number; version field is the first 4 bytes
- All shipped client files are version 41
- Strings use u1_wstr (u8 length + UTF-16LE chars) or u4_wstr (u32 length + UTF-16LE chars)
- Path waypoint configs use binary LDF (key-value pairs)
- Scene filenames use backslash separators, normalized to forward slashes by readers
- Verified against legouniverse.exe: ReadLUZFile @ 010438a0, ReadLUZPaths @ 0108caa0

## References

- lcdr/lu_formats luz.ksy (github.com/lcdr/lu_formats)
- DarkflameServer Zone.cpp LoadPath() (github.com/DarkflameUniverse/DarkflameServer)
- Ghidra RE of legouniverse.exe
