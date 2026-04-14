# lu-assets source

Format parsers for the LEGO Universe game client, organized by engine/vendor:

| Directory | Engine/Vendor | Formats |
|-----------|--------------|---------|
| [netdevil/](netdevil/) | NetDevil (game developer) | PK archives, SD0 compression, PKI pack index, FDB databases, LDF config, LUZ zones, LVL scenes, terrain, triggers, audio config, asset lists, macros |
| [gamebryo/](gamebryo/) | Gamebryo (rendering engine) | NIF meshes, KFM animation, settings |
| [havok/](havok/) | Havok (physics engine) | HKX collision data — binary packfile, tagged binary, XML |
| [lego/](lego/) | LEGO Group | LXFML brick models, brick geometry, brick assembly/colors |
| [fmod/](fmod/) | FMOD (audio engine) | FEV events, FSB sound banks |
| [forkparticle/](forkparticle/) | ForkParticle (particle engine) | PSB emitter definitions, effect files |
| [microsoft/](microsoft/) | Microsoft/DirectX | DDS textures, TGA images, FXO compiled shaders |
| [scaleform/](scaleform/) | Scaleform (UI engine) | GFX modified SWF files |
| [common/](common/) | Shared utilities | Binary reader, primitive geometry generation |
