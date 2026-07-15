// Copyright (c) 2026 The WORR Project
// Licensed under the GNU General Public License 2.0.

#pragma once

#include "../g_local.hpp"

#include <cstddef>
#include <cstdint>

// C++-safe mirror of the engine's WORR_REWIND_COLLISION_IMPORT_V1 ABI.  The
// engine header also imports the legacy C game ABI, which cannot coexist with
// sgame's modern C++ game types. Keep the provider layout asserted here. The
// trace output uses sgame's ABI-compatible `trace_t`; the provider never
// returns a live entity pointer, so the caller attaches one only after it has
// generation-validated the current mover.
namespace worr_sgame_rewind_collision {

constexpr const char kImportName[] = "WORR_REWIND_COLLISION_IMPORT_V1";
constexpr uint32_t kApiVersion = 1u;
constexpr uint32_t kSchemaVersion = 1u;
constexpr uint32_t kAssetInlineBrush = 1u;

struct AssetHandle {
  uint32_t map_epoch;
  uint32_t asset_id;
  uint64_t asset_hash;
};

struct Map {
  uint32_t struct_size;
  uint32_t schema_version;
  uint32_t map_epoch;
  uint32_t map_checksum;
  uint32_t inline_model_count;
  uint32_t reserved0;
};

struct Asset {
  uint32_t struct_size;
  uint32_t schema_version;
  AssetHandle handle;
  uint32_t kind;
  uint32_t source_model_index;
  uint32_t map_checksum;
  uint32_t reserved0;
  float local_mins[3];
  float local_maxs[3];
};

struct TraceRequest {
  uint32_t struct_size;
  uint32_t schema_version;
  AssetHandle asset;
  uint32_t contents_mask;
  uint32_t reserved0;
  float start[3];
  float end[3];
  float mins[3];
  float maxs[3];
  float origin[3];
  float angles[3];
};

using GetMapIdentityFn = bool (*)(Map *map_out);
using ResolveInlineBrushFn = bool (*)(uint32_t expected_map_epoch,
                                      uint32_t game_model_index,
                                      Asset *asset_out);
using TraceTransformedFn = bool (*)(const TraceRequest *request,
                                    trace_t *trace_out);

struct Import {
  uint32_t struct_size;
  uint32_t api_version;
  GetMapIdentityFn GetMapIdentity;
  ResolveInlineBrushFn ResolveInlineBrush;
  TraceTransformedFn TraceTransformed;
};

static_assert(sizeof(AssetHandle) == 16,
              "rewind collision asset-handle ABI changed");
static_assert(sizeof(Map) == 24, "rewind collision map ABI changed");
static_assert(sizeof(Asset) == 64, "rewind collision asset ABI changed");
static_assert(sizeof(TraceRequest) == 104,
              "rewind collision trace-request ABI changed");
static_assert(offsetof(Asset, handle) == 8,
              "rewind collision asset-handle offset changed");
static_assert(offsetof(Asset, local_mins) == 40,
              "rewind collision local-minimum offset changed");
static_assert(offsetof(Asset, local_maxs) == 52,
              "rewind collision local-maximum offset changed");
static_assert(offsetof(TraceRequest, asset) == 8,
              "rewind collision trace asset offset changed");
static_assert(offsetof(TraceRequest, start) == 32,
              "rewind collision trace start offset changed");
static_assert(offsetof(TraceRequest, angles) == 92,
              "rewind collision trace angle offset changed");
static_assert(offsetof(Import, GetMapIdentity) == 8,
              "rewind collision import map callback offset changed");
static_assert(offsetof(Import, ResolveInlineBrush) ==
                  offsetof(Import, GetMapIdentity) + sizeof(GetMapIdentityFn),
              "rewind collision import resolver offset changed");
static_assert(offsetof(Import, TraceTransformed) ==
                  offsetof(Import, ResolveInlineBrush) +
                      sizeof(ResolveInlineBrushFn),
              "rewind collision import trace offset changed");
static_assert(sizeof(Import) == offsetof(Import, TraceTransformed) +
                                   sizeof(TraceTransformedFn),
              "rewind collision import ABI changed");

} // namespace worr_sgame_rewind_collision
