# Gamebryo Engine Formats

Formats from the Gamebryo/NetImmerse rendering engine used by LEGO Universe.

| Directory | Format | Description |
|-----------|--------|-------------|
| [nif/](nif/) | NIF (.nif, .kf, .etk) | Scene graph meshes, materials, textures, animations; .etk holds LU's NiExtraTextKeyExtraData event keys |
| [kfm/](kfm/) | KFM (.kfm) | Keyframe manager — model path, sequence list, and transition graph |
| [settings/](settings/) | Settings (.settings) | Binary settings exported by NiKFMTool/SequenceEditor |

Every format has a reader and a writer with byte-identical round-trips, verified against
all .nif/.kf/.etk/.kfm/.settings files in the shipped clients by
`tests/roundtrip_sweep.cpp` (run it with one or more client roots as arguments).
