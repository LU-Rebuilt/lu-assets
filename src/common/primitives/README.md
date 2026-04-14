# Primitive Geometry Generation

Procedural mesh generation for collision shape visualization and debug rendering.

**Functions:**
- `generate_box(cx, cy, cz, hx, hy, hz)` — axis-aligned box
- `generate_sphere(cx, cy, cz, radius, rings, sectors)` — UV sphere
- `generate_capsule(a, b, radius, rings, sectors)` — capsule between two points
- `generate_cylinder(a, b, radius, segments)` — cylinder between two points

All return `PrimitiveMesh` (interleaved xyz vertices + triangle indices). Used by the HKX viewer and navmesh tools for rendering Havok collision shapes.
