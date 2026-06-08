# UDMAP File Format

This document describes the Undecedent map file format implemented by
`src/map_io.cpp`.

The current format is chunked `.udmap` version 3. It uses a small binary
container for integrity, chunk reuse, and optional extension chunks. Most
chunk payloads are tokenized text. Embedded material textures use a binary
payload.

## Status

- File extension: `.udmap`
- Current magic: `UDMAP3\0\0`
- Current container version: `3`
- Legacy text magic still accepted: `UNDECEDENT_MAP`
- Legacy text versions accepted: `1` through `2`
- New saves always write chunked v3 files.

## Design Goals

The chunked format exists so the editor can save only changed parts of a map
while preserving unchanged payloads byte-for-byte when possible.

Stable IDs are central to that design:

- Every sector chunk has a non-zero stable ID in its directory entry.
- Player spawn and point lights have stable IDs inside the entity payload.
- Missing IDs are assigned during save.
- Dirty saves reuse existing sector chunks when the stable ID is unchanged and
  the sector is not dirty.
- Sector scripts refer to sectors by stable ID.

## Byte Order and Primitive Encoding

All integer fields in the binary container and binary texture payloads are
little-endian unsigned integers.

Text payloads are written with C++ stream formatting and
`std::numeric_limits<float>::max_digits10` precision for floats. Strings that
can contain spaces are written with `std::quoted`.

## Container Layout

A chunked v3 file is:

```text
file_header
chunk_directory[chunk_count]
payload_bytes...
```

### File Header

The file header is 32 bytes.

| Offset | Size | Type | Meaning |
| --- | ---: | --- | --- |
| 0 | 8 | bytes | Magic: `55 44 4d 41 50 33 00 00` (`UDMAP3\0\0`) |
| 8 | 4 | u32 | Container version. Must be `3`. |
| 12 | 4 | u32 | Chunk count. |
| 16 | 8 | u64 | Directory offset. Must be `32` in the current loader. |
| 24 | 8 | u64 | Payload offset. Must be after the directory. |

The current writer always sets `directory_offset` to `32` and `payload_offset`
to `32 + chunk_count * 56`.

### Directory Entry

Each directory entry is 56 bytes.

| Offset | Size | Type | Meaning |
| --- | ---: | --- | --- |
| 0 | 4 | FourCC/u32 | Chunk type, stored as its ASCII bytes. |
| 4 | 4 | u32 | Flags. |
| 8 | 8 | u64 | Chunk ID. Meaning depends on type. |
| 16 | 8 | u64 | Absolute payload offset from file start. |
| 24 | 8 | u64 | Payload size in bytes. |
| 32 | 4 | u32 | CRC-32 of the payload bytes. |
| 36 | 4 | u32 | Reserved, written as `0`. |
| 40 | 8 | u64 | Reserved, written as `0`. |
| 48 | 8 | u64 | Reserved, written as `0`. |

The CRC-32 algorithm is the standard reflected polynomial `0xedb88320`, with
initial value `0xffffffff` and final bitwise inversion.

### Chunk Flags

| Flag | Value | Meaning |
| --- | ---: | --- |
| Optional | `1 << 0` | Unknown optional chunks may be skipped. Known chunks that are expected to be optional, such as `MTEX`, must carry this flag. |

Unknown required chunks make load fail. Unknown optional chunks are ignored.

## Chunk Types

| FourCC | Required | ID | Payload |
| --- | --- | --- | --- |
| `META` | Yes | `0` | Text metadata payload. Current loader only requires the chunk to exist and pass checksum. |
| `ENTY` | Yes | `0` | Text entity payload. |
| `SECT` | Required per sector | Sector stable ID | Text sector payload. Zero sectors are valid and therefore require no `SECT` chunk. |
| `LITE` | No | `0` | Optional text world lighting payload. |
| `MATS` | No | `0` | Optional text material library payload. Missing chunk uses the default material library. |
| `MTEX` | No | Material slot index | Optional binary embedded material texture payload. |
| `SCRP` | No | `0` | Optional text script-store payload. |
| `EDST` | No | Any | Optional editor-state chunk. Currently ignored on load. |

`META` and `ENTY` must be present. `ENTY` may appear only once.

`SECT` chunks must have non-zero, unique IDs. Their order in the directory is
the loaded sector order.

`MTEX` IDs must be material slot indices in `[0, 7]`. Duplicate `MTEX` chunks
for the same material slot are rejected.

## Written Chunk Order

Full saves currently write chunks in this order:

1. `META`
2. `MATS`
3. `LITE`
4. `ENTY`
5. `SCRP`, only when the script store is not empty
6. One `SECT` chunk per sector
7. One `MTEX` chunk per material slot with embedded texture bytes

Dirty saves follow the same logical order, but may reuse payloads from an
existing file when the matching chunk is unchanged and its CRC matches.

## META Payload

The writer emits:

```text
meta 1
format 3
generator Undecedent
next_id <u64>
```

`next_id` is the next stable ID after assigning IDs to sectors, player spawn,
and point lights. Current load code does not parse these fields; it only
requires a valid `META` chunk to exist.

## SECT Payload

A sector payload stores one sector record:

```text
sector
height <float>
floor <float>
outer <vertex_count>
v <x> <y>
...
holes <hole_count>
hole <vertex_count>
v <x> <y>
...
materials <floor_material> <ceiling_material>
wall_materials <outer_vertex_count> <material_id>...
hole_wall_materials <hole_count>
hole_wall_materials <hole_vertex_count> <material_id>...
...
surface_displacement ...
surface_displacement_sparse ...
endsector
```

### Geometry

`outer` and each `hole` are polygon loops. A loop must have at least three
vertices.

```text
outer <count>
v <x> <y>
...

hole <count>
v <x> <y>
...
```

The loader rebuilds triangulation from `outer` and `holes`. Invalid polygons
are rejected during load, including too few vertices and self-intersections.

After loading all sectors, exact edge adjacency is rebuilt. Edges become
neighbors only when their 2D endpoints are reversed matches and their vertical
ranges overlap.

### Height and Floor

`height` must be greater than zero.

`floor` is the sector floor height. New saves always write it. Legacy sector
records may omit `floor`; omitted floor height defaults to `0`.

The sector ceiling height is derived as:

```text
floor_height + height
```

### Materials

The current material count is `8`; valid material IDs are `0` through `7`.

Sector material assignments are:

- `floor_material`
- `ceiling_material`
- one wall material per outer edge
- one wall material per hole edge

The `wall_materials` count must equal the outer vertex count. The
`hole_wall_materials` group count must equal the hole count, and each group
count must equal that hole's vertex count.

If a sector record lacks a material block, materials are normalized to defaults.

### Dense Surface Displacement

Dense displacement stores every generated sample explicitly:

```text
surface_displacement <surface> <resolution> <sample_count>
surface_displacement_sample <x> <y> <offset>
...
```

`surface` is `floor` or `ceiling`.

`resolution` is clamped to the engine's displacement range:

- minimum: `1`
- default: `4`
- maximum: `16`

### Sparse Surface Displacement

Sparse displacement stores only non-zero offsets:

```text
surface_displacement_sparse <surface> <resolution> <total_sample_count> <changed_count>
surface_displacement_offset <sample_index> <offset>
...
```

Sparse records require the sector to be triangulatable at read time, because
the full sample grid is regenerated before applying offsets. `total_sample_count`
must match the generated sample count. Sparse sample indices must be in range
and unique.

The writer chooses sparse output only when it is smaller than dense output.
Offsets with absolute value at or below `0.001` are treated as unchanged for
sparse output.

## ENTY Payload

The current writer emits entity payload version 2:

```text
entities 2
player_spawn unset
point_lights <count>
...
```

or:

```text
entities 2
player_spawn <id> <x> <y> <z> <yaw>
point_lights <count>
point_light <id> <x> <y> <z> <r> <g> <b> <radius> <intensity> <shadow_bias>
...
```

Loader support:

- `entities 1`: supported, with point lights lacking `shadow_bias`.
- `entities 2`: current version, includes `shadow_bias`.

Validation:

- A set player spawn must have a non-zero ID.
- Point light IDs must be non-zero and unique within the payload.
- Point light radius must be greater than zero.
- Point light intensity must be non-negative.
- Point light shadow bias must be non-negative.
- All floats must be finite.

Default authored values from `geometry.hpp`:

- Player spawn position: `(0, 48, 0)`
- Player spawn yaw: `0`
- Point light position: `(0, 64, 0)`
- Point light color: `(1, 0.86, 0.62)`
- Point light radius: `384`
- Point light intensity: `1.5`
- Point light shadow bias: `2`

## LITE Payload

World lighting is optional. Missing `LITE` uses the default `WorldLighting`.

Current payload:

```text
lighting 1
sun <enabled> <dir_x> <dir_y> <dir_z> <r> <g> <b> <intensity>
```

Validation:

- `enabled` must be `0` or `1`.
- Direction is normalized on load and save; invalid or zero direction falls
  back to the default sun direction.
- Color components must be non-negative.
- Intensity must be non-negative.
- All floats must be finite.

Default lighting from `geometry.hpp`:

```text
sun_enabled   = true
sun_direction = (-0.348155, -0.870388, -0.348155)
sun_color     = (1, 0.94, 0.78)
sun_intensity = 0.65
```

## MATS Payload

The current writer emits material payload version 3:

```text
materials 3
count 8
slot <index> <r> <g> <b> <roughness> <metallic> <specular> <uv_scale> "<albedo_texture_path>" <image_codec> <storage_mode> <jxl_quality>
...
```

Loader support:

- `materials 1`: accepted, but ignores slots and uses defaults.
- `materials 2`: accepted, reads slot properties and path. Texture codec is
  inferred from path. Storage mode defaults to source bytes.
- `materials 3`: current version, includes image codec, storage mode, and JPEG
  XL quality.

There must be exactly 8 slots, with every index `0` through `7` present exactly
once.

### Material Slot Fields

| Field | Meaning |
| --- | --- |
| `index` | Material slot index in `[0, 7]`. |
| `r g b` | Base color. Normalized to `[0, 1]` with defaults as fallback. |
| `roughness` | Normalized to `[0.04, 1]` with defaults as fallback. |
| `metallic` | Normalized to `[0, 1]` with defaults as fallback. |
| `specular` | Normalized to `[0, 1]` with defaults as fallback. |
| `uv_scale` | Must be greater than zero after normalization, otherwise default. |
| `albedo_texture_path` | Quoted generic path string. Used as a fallback and for source embedding. |
| `image_codec` | Source/embedded image codec. |
| `storage_mode` | Preferred storage mode for saving embedded bytes. |
| `jxl_quality` | JPEG XL lossy quality, normalized to `[1, 100]`, default `80`. |

### Image Codecs

| Value | Name | Meaning |
| ---: | --- | --- |
| 0 | `SdlSurfaceImage` | Decode using the SDL image path. Used for non-`.jxl` sources. |
| 1 | `JpegXl` | Decode as JPEG XL. Used for `.jxl` sources or transcoded storage. |

### Texture Storage Modes

| Value | Name | Meaning |
| ---: | --- | --- |
| 0 | `SourceBytes` | Store original texture bytes. |
| 1 | `JpegXlLossless` | Decode texture and store JPEG XL lossless bytes when encoding succeeds. |
| 2 | `JpegXlLossy` | Decode texture and store JPEG XL lossy bytes using `jxl_quality` when encoding succeeds. |

If JPEG XL transcode fails, saving falls back to source bytes and records a
warning in the save message.

## MTEX Payload

`MTEX` chunks embed material texture bytes. They are optional chunks. The chunk
ID is the material slot index.

On save, a material slot gets an `MTEX` chunk when `albedo_texture_bytes` is not
empty. If bytes are empty but `albedo_texture_path` is set, the saver tries to
read the texture source file:

1. Absolute path, if the path is absolute.
2. Path relative to the `.udmap` file directory.
3. Path relative to the current working directory.

If the source file cannot be read, save still succeeds and returns a warning.

### MTEX Version 2

Current binary payload:

| Offset | Size | Type | Meaning |
| --- | ---: | --- | --- |
| 0 | 4 | u32 | Payload version. Must be `2`. |
| 4 | 4 | u32 | Image codec. See image codec table above. |
| 8 | 4 | u32 | Compression codec. |
| 12 | 4 | u32 | Texture name byte length. |
| 16 | 8 | u64 | Uncompressed texture byte count. |
| 24 | 8 | u64 | Stored texture byte count. |
| 32 | 4 | u32 | CRC-32 of uncompressed texture bytes. |
| 36 | name length | bytes | Texture name bytes. |
| 36 + name length | stored byte count | bytes | Stored texture payload. |

Supported compression codecs:

| Value | Name | Meaning |
| ---: | --- | --- |
| 0 | `None` | Stored bytes are raw texture bytes. |
| 1 | `XzLzma2` | Stored bytes are LZMA2/XZ-compressed texture bytes. |

The writer tries LZMA2/XZ and uses it only when the compressed payload is
smaller than the uncompressed payload.

Validation:

- Image codec must be supported.
- Compression codec must be supported.
- Uncompressed and stored sizes must be non-zero.
- Neither size may exceed 256 MiB.
- Payload size must exactly equal header + name + stored bytes.
- If compression is `None`, stored size must equal uncompressed size.
- After optional decompression, the uncompressed bytes must match the CRC-32.

### MTEX Version 1

The loader still accepts legacy material texture payload version 1:

| Offset | Size | Type | Meaning |
| --- | ---: | --- | --- |
| 0 | 4 | u32 | Payload version: `1`. |
| 4 | 4 | u32 | Texture name byte length. |
| 8 | 8 | u64 | Texture byte count. |
| 16 | name length | bytes | Texture name bytes. |
| 16 + name length | byte count | bytes | Raw texture bytes. |

Version 1 texture bytes are interpreted as `SdlSurfaceImage`.

## SCRP Payload

`SCRP` stores compiled script programs, not source text.

Current writer emits:

```text
scripts 2
global <0-or-1>
program ...
entity_scripts <count>
entity_script <entity_id>
program ...
sector_scripts <count>
sector_script <sector_id>
program ...
```

Loader support:

- `scripts 1`: supports global and entity scripts.
- `scripts 2`: current version, also supports sector scripts.

If a sector script refers to a sector ID that is not present in any `SECT`
chunk, the map is rejected.

### Program Records

```text
program <instruction_count> <function_count> <comment_count>
function "<name>" <entry> <arity>
...
comment <line> "<text>"
...
ins <opcode> <operand_i> <operand_f> <operand_u>
...
```

Validation:

- Function entries must point at an instruction index.
- Opcode names must be known to `script_opcode_from_name`.
- Entity and sector script IDs must be non-zero.
- Duplicate entity or sector script IDs are rejected.

Script payloads should be produced through `write_script_store_payload`, not
hand-authored, because they encode bytecode details that must match the VM.

## Legacy Text Format

The loader still accepts legacy text maps beginning with:

```text
UNDECEDENT_MAP <version>
```

Accepted versions are `1` and `2`.

Legacy body order:

```text
player_spawn unset
```

or:

```text
player_spawn <x> <y> <z> <yaw>
```

then optional point lights:

```text
point_lights <count>
point_light <x> <y> <z> <r> <g> <b> <radius> <intensity>
...
```

then sectors:

```text
sectors <count>
sector
height <float>
outer <count>
...
endsector
```

Legacy sector records are parsed with the same sector reader as v3, so they may
also contain newer sector fields such as `floor`, materials, and displacement
records. Legacy records without `floor` default to floor height `0`.

When a legacy map loads successfully:

- Stable IDs are assigned to sectors, player spawn, and point lights.
- Triangulation and adjacency are rebuilt.
- Materials, lighting, and scripts default to empty/default state.
- Re-saving writes chunked v3.

## Load-Time Normalization and Rebuilds

Successful load does more than deserialize bytes:

- Sector polygons are triangulated.
- Sector materials are clamped and resized to match geometry.
- Displacement data is normalized.
- Sector edge adjacency is rebuilt exactly.
- World lighting is normalized.
- Material slots are normalized.
- Embedded texture payloads are attached to material slots after `MATS` is read.

This means a loaded `SectorPlane` can differ from the raw payload in fields that
are derived or normalized.

## Save-Time Behavior

Full saves:

- Assign missing stable IDs.
- Normalize material library data.
- Try to embed texture bytes for path-only material textures.
- Generate sector triangulation if needed before writing displacement data.
- Choose dense or sparse displacement per surface based on payload size.
- Write to `<path>.tmp`, then replace the target map.

Dirty saves:

- If `dirty_state.topology` is true, fall back to full save.
- Otherwise parse the existing chunk directory.
- Reuse existing chunks when possible and when their CRCs are valid.
- Rewrite dirty sectors, metadata, entities, and scripts.
- Rebuild `MATS` and `MTEX` chunks from the current material library.
- Fall back to full save if the existing file is not valid chunked v3.

`MapDirtyState` fields:

| Field | Effect |
| --- | --- |
| `sector_ids` | Sector IDs that must be rewritten. |
| `entities` | Rewrite `ENTY`. |
| `metadata` | Rewrite `LITE`. The `META` chunk is always rewritten because it carries `next_id`. |
| `materials` | Present in the dirty-state API. Current dirty-save code rebuilds material chunks regardless of this flag. |
| `topology` | Force full save. |
| `scripts` | Rewrite `SCRP`, or remove it if the script store is empty. |

## Compatibility Rules for Future Chunks

To add data without breaking older loaders:

1. Put non-essential new data in a new optional chunk.
2. Set the optional flag on that chunk.
3. Keep existing required chunks readable.
4. Use a chunk ID that matches the data's stable identity when partial reuse is
   valuable.

To add required semantics that old loaders must reject, add a required chunk
with a new FourCC or change the container version. Unknown required chunks are
already rejected.

## Minimal Chunked Map Example

This is a conceptual view. The real file starts with a binary header and
directory; payloads are shown as text.

```text
META id=0 required:
meta 1
format 3
generator Undecedent
next_id 2

MATS id=0 optional:
materials 3
count 8
slot 0 ...
...

LITE id=0 optional:
lighting 1
sun 1 -0.348155 -0.870388 -0.348155 1 0.94 0.78 0.65

ENTY id=0 required:
entities 2
player_spawn unset
point_lights 0

SECT id=1 required:
sector
height 96
floor 0
outer 4
v 0 0
v 128 0
v 128 128
v 0 128
holes 0
materials 0 0
wall_materials 4 0 0 0 0
hole_wall_materials 0
endsector
```
