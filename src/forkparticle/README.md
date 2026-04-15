# ForkParticle Formats

Formats from the ForkParticle particle system used by LEGO Universe.

Reverse-engineered from legouniverse.exe, the ForkParticle MediaFX editor (mediafx.exe), and its plugin DLLs (RenderParticleDLL.dll, GeomObjectDLL.dll, ParticleBirthDLL.dll).

| Directory | Format | Description |
|-----------|--------|-------------|
| [psb/](psb/) | PSB (.psb) | Binary emitter definitions — colors, scale, rotation, velocity, emission, textures, animation |
| [effect/](effect/) | Effect (.txt) | Plain-text effect definitions referencing one or more PSB emitters with transforms and flags |

## RE Sources

- **legouniverse.exe** — PSB loader (FUN_01092450), emitter initializer (FUN_010cdbf0), particle update (FUN_0109bb30), physics/drag (FUN_01093fc0), emission (FUN_010d2f90), spawn (FUN_01097c60), effect parser (FUN_010c8610)
- **mediafx.exe** — CEmitterStatsDlg (FUN_0046f890) for field name/offset mapping, PSB text serializer (FUN_004179b0) for official ForkParticle tag names
- **RenderParticleDLL.dll** — Sprite-based particle rendering (billboard and velocity-aligned modes)
- **GeomObjectDLL.dll** — Model-based particle rendering (.PAX DirectX mesh format)
- **ParticleBirthDLL.dll** — Particle birth/spawning plugin
