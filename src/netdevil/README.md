# NetDevil Formats

Proprietary formats created by NetDevil for the LEGO Universe client.

| Directory | Format | Description |
|-----------|--------|-------------|
| [archive/pk/](archive/pk/) | PK (.pk) | NDPK archive — packed asset containers with SD0 compression |
| [archive/sd0/](archive/sd0/) | SD0 | Segmented zlib compression used inside PK archives |
| [archive/pki/](archive/pki/) | PKI (.pki) | Pack index — maps CRC hashes to pack files |
| [database/fdb/](database/fdb/) | FDB (.fdb) | Flat database — CDClient game data (converts to SQLite) |
| [common/ldf/](common/ldf/) | LDF | Key-value config format (text and binary variants) |
| [zone/luz/](zone/luz/) | LUZ (.luz) | Zone files — scenes, paths, transitions, spawn points |
| [zone/lvl/](zone/lvl/) | LVL (.lvl) | Scene/level files — chunked format with objects and environment |
| [zone/terrain/](zone/terrain/) | Terrain (.raw) | Heightmap terrain with texture layers and decorations |
| [zone/lutriggers/](zone/lutriggers/) | LU Triggers (.lutriggers) | XML trigger event/command definitions |
| [zone/aud/](zone/aud/) | AUD (.aud) | XML audio configuration per scene |
| [zone/zal/](zone/zal/) | ZAL (.zal) | Zone asset lists (plain text) |
| [zone/ast/](zone/ast/) | AST (.ast) | Asset lists with "A:" prefix (plain text) |
| [macros/scm/](macros/scm/) | SCM (.scm) | Script command macros (plain text) |
