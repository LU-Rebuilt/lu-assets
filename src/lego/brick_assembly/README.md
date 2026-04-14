# Brick Assembly

Converts LXFML brick models into renderable geometry. Takes parsed `LxfmlFile` + a geometry loader callback, and produces `AssembledBrick` meshes with per-brick colors.

- `assemble_lxfml()` — main assembly pipeline
- `brick_color_lookup()` — LEGO material ID → RGBA color (80+ materials)

References: [lu-toolbox](https://github.com/Squareville/lu-toolbox) for assembly pipeline and color database.
