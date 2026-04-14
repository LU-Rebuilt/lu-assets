# Brick Assembly

Converts LXFML brick models into renderable geometry with material colors.

## Pipeline

```
LxfmlFile → assemble_lxfml(lxfml, loader) → AssemblyResult
                                   ↑
                        BrickGeometryLoader callback
                        (loads .g files by designID)
```

1. Iterates each brick in the LXFML model
2. Calls the loader callback to get `.g` geometry for each brick's design ID
3. Applies the brick's 4x4 transform matrix to all vertices and normals
4. Looks up the brick's material ID → RGBA color via `brick_color_lookup()`
5. Produces one `AssembledBrick` per brick with world-space vertices

## API

| Function | Description |
|----------|-------------|
| `assemble_lxfml(lxfml, loader)` | Assemble full model → `AssemblyResult` with per-brick meshes |
| `brick_color_lookup(material_id)` | LEGO material ID → `BrickColor` RGBA (80+ materials supported) |

## Color Database

The color lookup table maps LEGO material IDs to RGBA values. Covers all materials used in LEGO Universe including:
- Standard colors (Bright Red 21, Bright Blue 23, Bright Yellow 24, etc.)
- Dark/medium variants
- Transparent materials (alpha < 1.0)
- Metallic and special finishes

## References

- [lu-toolbox](https://github.com/Squareville/lu-toolbox) — assembly pipeline and material color database
- [NexusDashboard](https://github.com/DarkflameUniverse/NexusDashboard) — LXFML rendering pipeline reference
