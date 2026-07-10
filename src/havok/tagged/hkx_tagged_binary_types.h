#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace lu::assets {

// ============================================================================
// HKX tagged binary format — raw-block-preservation model (sibling of
// havok/packfile/, which handles the OTHER `.hkx` container: binary packfile,
// magic 0x57E0E057 0x10C0C010). This module handles the tagged binary variant,
// magic 0xCAB00D1E 0xD011FACE.
// ============================================================================
//
// Unlike packfile (a fixed-header + section-table container), tagged binary is a
// stream/tag-driven format: varint-encoded tags dispatch to fileinfo, type
// metadata, and object bodies, with an inline string pool (new strings carry
// their byte length; backreferences carry a negative "index into pool" varint)
// used both by the type table and by object data.
//
// A pre-existing, intentionally lossy semantic reader for this format already
// exists at havok/reader/hkx_tagged_reader.{h,cpp} (namespace Hkx::) — it walks
// the full object graph, extracting hkpRigidBody/shape/etc. fields for physics
// analysis. This module does NOT reuse or replace that reader; it solves a
// different problem (byte-perfect round-trip) with a different, independent
// design, following the same "container-level, not object-level" scope this
// project used for havok/packfile/.
//
// Corpus survey (see README.md for full methodology) of all 823 real
// tagged-binary .hkx files under vanilla_unpacked found:
//
//   - Every file has EXACTLY this top-level shape, with no variation:
//       magic (8 bytes)
//       tag=1 (fileinfo) + version varint            -- version is always 0
//       N x [tag=2 (type definition) + type record]   -- N ranges 32-100
//       tag=3 (root object) + ... rest of file ...
//     Never tag=4 for the root object, never more than one fileinfo, never a
//     type tag appearing after the root object starts.
//
//   - The header+type-table region uses ONLY canonical (minimal-length) varint
//     encodings: verified by re-deriving the canonical encoding for all
//     964,859 varints in that region across all 823 files and finding zero
//     that don't already use the minimal-length form. This means the type
//     table can be safely parsed into plain integers/strings and *re-encoded*
//     (not stored as raw bytes) while still reproducing the original bytes
//     exactly.
//
//   - BUT the object stream that follows the root object's tag is a different
//     story: walking it with the exact same semantics as the pre-existing
//     lossy Hkx::HkxTaggedReader (which is a faithful, Ghidra-RE-confirmed
//     implementation) leaves nonzero trailing, unconsumed bytes in 716/823
//     files (87%) -- not a rare edge case, the majority case. Deep-diving one
//     example (res/BrickModels/ndmade/00000000000000010426.hkx) traced this to
//     a concrete, reproducible quirk: hkMemoryResourceContainer::resourceHandles
//     (a TB_Object array of hkMemoryResourceHandle) is declared with array
//     length 2 in EVERY sampled file that has this field, but only ONE
//     tag-prefixed object body is ever actually present at that position --
//     the "second element" position instead holds a tag varint that isn't one
//     of the reader's known dispatch values (0/3/4/5/6), immediately followed
//     by more struct-shaped float/int data that never gets consumed by the
//     object-graph walk. This looks like a genuine, second Havok-internal
//     encoding path this project's semantic reader doesn't yet model (object
//     "weak references" or an unresolved second serialization convention for
//     resource-handle arrays specifically -- Havok's hkMemoryResourceContainer
//     is bookkeeping infrastructure, not gameplay-relevant physics data, so the
//     lossy reader was never extended to cover it). Since this module's job is
//     container-level byte fidelity, not object semantics, it does not need to
//     resolve this to round-trip correctly -- see below.
//
// Design: given the above, the object stream (everything from the root
// object's tag byte through end-of-file) is preserved as ONE opaque raw byte
// blob, exactly like a packfile section's sub-regions. This sidesteps the
// resourceHandles quirk (and any other undiscovered ones like it) entirely:
// the bytes are captured and replayed verbatim, whatever they encode. Only the
// header (magic, fileinfo) and the type table are parsed into structured
// fields, because that region survey-verified as clean, small, and safe to
// re-encode losslessly.
// ============================================================================

struct HkxTaggedBinaryError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// One member (field) of a type definition. Mirrors the wire encoding directly:
// `type` is baseType (low 4 bits) | TupleFlag (0x20) | ArrayFlag (0x10), exactly
// as the pre-existing Hkx::TaggedMember models it (see
// havok/reader/hkx_tagged_reader.h) -- this module defines its own copy rather
// than reusing that struct, since ours needs to be independently
// round-trip-safe (e.g. preserving `type` as the raw wire byte rather than
// decomposing it into separate enum fields that might not re-encode
// identically).
struct HkxTaggedMember {
    std::string name;
    uint8_t type = 0;        // baseType | TupleFlag(0x20) | ArrayFlag(0x10), same encoding as the wire byte
    int32_t tupleSize = 0;    // only present on the wire (and thus only meaningful) when type & TupleFlag
    std::string className;    // only present on the wire when (type & 0x0F) is Object(8) or Struct(9)
};

// One type definition record (wire tag 2). `version` is the type's own schema
// version varint (NOT the file's fileinfo version) -- confirmed non-constant
// across real types (e.g. hkxScene has version 1 while most others have 0), so
// it must be stored and replayed per-type, not assumed.
struct HkxTaggedType {
    std::string name;
    int32_t version = 0;
    int32_t parentIndex = 0;  // 0 = no parent; otherwise a 1-based index into the file's type list
    std::vector<HkxTaggedMember> members;
};

// A parsed HKX tagged binary file: structured fileinfo + type table, plus the
// entire remaining object stream preserved as one opaque blob. See the
// module-level comment above for why the object stream isn't parsed further.
struct HkxTaggedBinary {
    uint32_t magic0 = 0xCAB00D1E;
    uint32_t magic1 = 0xD011FACE;
    int32_t fileInfoVersion = 0;  // Always 0 in the real corpus (823/823), but stored, not assumed.
    std::vector<HkxTaggedType> types;

    // Raw bytes from the root object's tag byte (wire tag 3, confirmed the
    // only value ever seen for the root object across the real corpus) through
    // end of file. Concatenating magic + re-encoded fileinfo/type-table +
    // this blob reproduces the file exactly.
    std::vector<uint8_t> objectStream;
};

} // namespace lu::assets
