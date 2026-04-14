# BinaryReader

Endian-aware binary stream reader used by all format parsers. Wraps a `std::span<const uint8_t>` and provides typed read methods:

- `read_u8()`, `read_u16()`, `read_u32()`, `read_u64()`
- `read_i8()`, `read_i16()`, `read_i32()`
- `read_f32()`, `read_f64()`
- `read_bytes(n)`, `read_string(n)`
- `skip(n)`, `seek(pos)`, `pos()`, `remaining()`

All multi-byte reads are little-endian (matching the LU client's x86 platform).
