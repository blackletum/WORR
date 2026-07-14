/* Standalone FR-10-T10 immutable brush-collision provider tests. */

#include "server/rewind_collision.h"

#include "../../src/server/server.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "server_rewind_collision_test:%d: %s\n",       \
                    __LINE__, #expression);                                   \
            exit(EXIT_FAILURE);                                               \
        }                                                                     \
    } while (0)

server_static_t svs;
server_t sv;

static bsp_t test_bsp;
static mmodel_t test_models[MODELINDEX_PLAYER + 8];
static mnode_t test_nodes[MODELINDEX_PLAYER + 8];
static csurface_t test_surface;
static csurface_t test_surface2;

typedef struct trace_capture_s {
    const mnode_t *headnode;
    int brushmask;
    bool extended;
    float start[3];
    float end[3];
    float mins[3];
    float maxs[3];
    float origin[3];
    float angles[3];
    uint32_t calls;
} trace_capture_t;

static trace_capture_t trace_capture;
static trace_t trace_template;
static bool emit_invalid_trace;

void CM_TransformedBoxTrace(trace_t *trace,
                            const vec3_t start, const vec3_t end,
                            const vec3_t mins, const vec3_t maxs,
                            const mnode_t *headnode, int brushmask,
                            const vec3_t origin, const vec3_t angles,
                            bool extended)
{
    ++trace_capture.calls;
    trace_capture.headnode = headnode;
    trace_capture.brushmask = brushmask;
    trace_capture.extended = extended;
    memcpy(trace_capture.start, start, sizeof(trace_capture.start));
    memcpy(trace_capture.end, end, sizeof(trace_capture.end));
    memcpy(trace_capture.mins, mins, sizeof(trace_capture.mins));
    memcpy(trace_capture.maxs, maxs, sizeof(trace_capture.maxs));
    memcpy(trace_capture.origin, origin, sizeof(trace_capture.origin));
    memcpy(trace_capture.angles, angles, sizeof(trace_capture.angles));
    *trace = trace_template;
    if (emit_invalid_trace)
        trace->fraction = NAN;
}

static void set_model(uint32_t index, float extent)
{
    mmodel_t *model = &test_models[index];

    memset(model, 0, sizeof(*model));
    model->headnode = &test_nodes[index];
    model->mins[0] = -extent;
    model->mins[1] = -extent - 1.0f;
    model->mins[2] = -extent - 2.0f;
    model->maxs[0] = extent;
    model->maxs[1] = extent + 1.0f;
    model->maxs[2] = extent + 2.0f;
}

static void reset_fixture(void)
{
    memset(&svs, 0, sizeof(svs));
    memset(&sv, 0, sizeof(sv));
    memset(&test_bsp, 0, sizeof(test_bsp));
    memset(test_models, 0, sizeof(test_models));
    memset(test_nodes, 0, sizeof(test_nodes));
    memset(&test_surface, 0, sizeof(test_surface));
    memset(&test_surface2, 0, sizeof(test_surface2));
    memset(&trace_capture, 0, sizeof(trace_capture));
    memset(&trace_template, 0, sizeof(trace_template));
    emit_invalid_trace = false;

    test_bsp.models = test_models;
    test_bsp.nummodels = MODELINDEX_PLAYER + 5;
    test_bsp.checksum = UINT32_C(0x91a2b3c4);
    set_model(1, 16.0f);
    set_model(2, 24.0f);
    set_model(MODELINDEX_PLAYER - 1u, 32.0f);
    set_model(MODELINDEX_PLAYER, 40.0f);

    sv.state = ss_loading;
    sv.worr_snapshot_epoch = 17;
    sv.cm.cache = &test_bsp;
    sv.cm.checksum = (int)UINT32_C(0x91a2b3c4);
    svs.csr.max_models = MAX_MODELS;
    svs.csr.extended = true;

    trace_template.fraction = 0.25f;
    trace_template.endpos[0] = 4.0f;
    trace_template.endpos[1] = 5.0f;
    trace_template.endpos[2] = 6.0f;
    trace_template.plane.normal[0] = 1.0f;
    trace_template.plane.dist = 7.0f;
    trace_template.surface = &test_surface;
    trace_template.contents = CONTENTS_SOLID;
    trace_template.ent = (edict_t *)(uintptr_t)1;
    trace_template.plane2.normal[1] = 1.0f;
    trace_template.plane2.dist = 8.0f;
    trace_template.surface2 = &test_surface2;
}

static worr_rewind_collision_asset_v1 resolve_asset(
    const worr_rewind_collision_import_v1 *api, uint32_t model_index)
{
    worr_rewind_collision_asset_v1 asset;

    memset(&asset, 0, sizeof(asset));
    CHECK(api->ResolveInlineBrush(sv.worr_snapshot_epoch, model_index,
                                  &asset));
    return asset;
}

static void test_map_and_asset_resolution(void)
{
    const worr_rewind_collision_import_v1 *api =
        SV_RewindCollisionImportV1();
    worr_rewind_collision_map_v1 map;
    worr_rewind_collision_map_v1 map_before;
    worr_rewind_collision_asset_v1 asset;
    worr_rewind_collision_asset_v1 repeat;
    worr_rewind_collision_asset_v1 hole_asset;
    worr_rewind_collision_asset_v1 zero_asset;
    worr_rewind_collision_asset_v1 asset_before;

    CHECK(api && api->struct_size == sizeof(*api));
    CHECK(api->api_version == WORR_REWIND_COLLISION_API_VERSION);
    CHECK(api->GetMapIdentity && api->ResolveInlineBrush &&
          api->TraceTransformed);

    memset(&map, 0, sizeof(map));
    CHECK(api->GetMapIdentity(&map));
    CHECK(map.struct_size == sizeof(map));
    CHECK(map.schema_version == WORR_REWIND_COLLISION_SCHEMA_VERSION);
    CHECK(map.map_epoch == 17);
    CHECK(map.map_checksum == UINT32_C(0x91a2b3c4));
    CHECK(map.inline_model_count == (uint32_t)test_bsp.nummodels - 1u);

    asset = resolve_asset(api, 2);
    CHECK(asset.struct_size == sizeof(asset));
    CHECK(asset.schema_version == WORR_REWIND_COLLISION_SCHEMA_VERSION);
    CHECK(asset.handle.map_epoch == 17 && asset.handle.asset_id == 1);
    CHECK(asset.handle.asset_hash != 0);
    CHECK(asset.kind == WORR_REWIND_COLLISION_ASSET_INLINE_BRUSH);
    CHECK(asset.source_model_index == 2);
    CHECK(asset.map_checksum == UINT32_C(0x91a2b3c4));
    CHECK(asset.local_mins[0] == -16.0f &&
          asset.local_maxs[2] == 18.0f);
    repeat = resolve_asset(api, 2);
    CHECK(memcmp(&asset, &repeat, sizeof(asset)) == 0);

    /* Configstring/model index 255 is reserved.  Ordinal 254 maps to 256. */
    CHECK(!api->ResolveInlineBrush(17, MODELINDEX_PLAYER, &repeat));
    hole_asset = resolve_asset(api, MODELINDEX_PLAYER + 1u);
    CHECK(hole_asset.handle.asset_id == MODELINDEX_PLAYER - 1u);
    CHECK(hole_asset.source_model_index == MODELINDEX_PLAYER + 1u);
    CHECK(hole_asset.local_mins[0] == -32.0f);

    test_models[2].mins[0] = -0.0f;
    test_models[2].maxs[0] = 0.0f;
    zero_asset = resolve_asset(api, 3);
    CHECK(zero_asset.local_mins[0] == 0.0f &&
          !signbit(zero_asset.local_mins[0]));

    memset(&asset_before, 0xa5, sizeof(asset_before));
    repeat = asset_before;
    CHECK(!api->ResolveInlineBrush(16, 2, &repeat));
    CHECK(memcmp(&repeat, &asset_before, sizeof(repeat)) == 0);
    CHECK(!api->ResolveInlineBrush(17, 0, &repeat));
    CHECK(memcmp(&repeat, &asset_before, sizeof(repeat)) == 0);
    CHECK(!api->ResolveInlineBrush(17, 1, &repeat));
    CHECK(memcmp(&repeat, &asset_before, sizeof(repeat)) == 0);
    CHECK(!api->ResolveInlineBrush(17, MAX_MODELS, &repeat));
    CHECK(memcmp(&repeat, &asset_before, sizeof(repeat)) == 0);
    test_models[1].mins[0] = NAN;
    CHECK(!api->ResolveInlineBrush(17, 2, &repeat));
    CHECK(memcmp(&repeat, &asset_before, sizeof(repeat)) == 0);
    test_models[1].mins[0] = -16.0f;
    CHECK(!api->ResolveInlineBrush(17, 2, NULL));

    map_before = map;
    memset(&map, 0xa5, sizeof(map));
    sv.state = ss_dead;
    CHECK(!api->GetMapIdentity(&map));
    CHECK(map.struct_size == UINT32_C(0xa5a5a5a5));
    sv.state = ss_game;
    CHECK(api->GetMapIdentity(&map));
    CHECK(memcmp(&map, &map_before, sizeof(map)) == 0);
    CHECK(!api->GetMapIdentity(NULL));
}

static worr_rewind_collision_trace_request_v1 make_request(
    worr_rewind_collision_asset_handle_v1 asset)
{
    worr_rewind_collision_trace_request_v1 request;

    memset(&request, 0, sizeof(request));
    request.struct_size = sizeof(request);
    request.schema_version = WORR_REWIND_COLLISION_SCHEMA_VERSION;
    request.asset = asset;
    request.contents_mask = CONTENTS_SOLID | CONTENTS_PROJECTILE;
    request.start[0] = 1.0f;
    request.start[1] = 2.0f;
    request.start[2] = 3.0f;
    request.end[0] = 11.0f;
    request.end[1] = 12.0f;
    request.end[2] = 13.0f;
    request.mins[0] = request.mins[1] = -2.0f;
    request.mins[2] = -4.0f;
    request.maxs[0] = request.maxs[1] = 2.0f;
    request.maxs[2] = 4.0f;
    request.origin[0] = 100.0f;
    request.origin[1] = 200.0f;
    request.origin[2] = 300.0f;
    request.angles[0] = 10.0f;
    request.angles[1] = 20.0f;
    request.angles[2] = 30.0f;
    return request;
}

static void test_explicit_transformed_trace(void)
{
    const worr_rewind_collision_import_v1 *api =
        SV_RewindCollisionImportV1();
    const worr_rewind_collision_asset_v1 asset = resolve_asset(api, 2);
    worr_rewind_collision_trace_request_v1 request =
        make_request(asset.handle);
    trace_t result;

    memset(&result, 0, sizeof(result));
    CHECK(api->TraceTransformed(&request, &result));
    CHECK(trace_capture.calls == 1);
    CHECK(trace_capture.headnode == test_models[1].headnode);
    CHECK(trace_capture.brushmask == (int32_t)request.contents_mask);
    CHECK(trace_capture.extended);
    CHECK(memcmp(trace_capture.start, request.start,
                 sizeof(request.start)) == 0);
    CHECK(memcmp(trace_capture.end, request.end, sizeof(request.end)) == 0);
    CHECK(memcmp(trace_capture.mins, request.mins,
                 sizeof(request.mins)) == 0);
    CHECK(memcmp(trace_capture.maxs, request.maxs,
                 sizeof(request.maxs)) == 0);
    CHECK(memcmp(trace_capture.origin, request.origin,
                 sizeof(request.origin)) == 0);
    CHECK(memcmp(trace_capture.angles, request.angles,
                 sizeof(request.angles)) == 0);
    CHECK(result.fraction == trace_template.fraction);
    CHECK(memcmp(result.endpos, trace_template.endpos,
                 sizeof(result.endpos)) == 0);
    CHECK(result.surface == &test_surface &&
          result.surface2 == &test_surface2);
    CHECK(result.ent == NULL);
}

static void test_trace_rejections_are_transactional(void)
{
    const worr_rewind_collision_import_v1 *api =
        SV_RewindCollisionImportV1();
    worr_rewind_collision_asset_v1 asset = resolve_asset(api, 2);
    worr_rewind_collision_trace_request_v1 request =
        make_request(asset.handle);
    trace_t output;
    trace_t before;
    uint32_t calls;
    union {
        worr_rewind_collision_trace_request_v1 request;
        trace_t trace;
    } alias;
    unsigned char alias_before[sizeof(alias)];

    memset(&output, 0xa5, sizeof(output));
    before = output;
    calls = trace_capture.calls;
    request.asset.asset_hash ^= UINT64_C(1);
    CHECK(!api->TraceTransformed(&request, &output));
    CHECK(memcmp(&output, &before, sizeof(output)) == 0);
    CHECK(trace_capture.calls == calls);

    request = make_request(asset.handle);
    request.asset.map_epoch--;
    CHECK(!api->TraceTransformed(&request, &output));
    CHECK(memcmp(&output, &before, sizeof(output)) == 0);
    request = make_request(asset.handle);
    request.reserved0 = 1;
    CHECK(!api->TraceTransformed(&request, &output));
    CHECK(memcmp(&output, &before, sizeof(output)) == 0);
    request = make_request(asset.handle);
    request.start[0] = NAN;
    CHECK(!api->TraceTransformed(&request, &output));
    CHECK(memcmp(&output, &before, sizeof(output)) == 0);
    request = make_request(asset.handle);
    request.mins[0] = 3.0f;
    request.maxs[0] = 2.0f;
    CHECK(!api->TraceTransformed(&request, &output));
    CHECK(memcmp(&output, &before, sizeof(output)) == 0);
    CHECK(!api->TraceTransformed(NULL, &output));
    CHECK(!api->TraceTransformed(&request, NULL));

    memset(&alias, 0, sizeof(alias));
    alias.request = make_request(asset.handle);
    memcpy(alias_before, &alias, sizeof(alias));
    CHECK(!api->TraceTransformed(&alias.request, &alias.trace));
    CHECK(memcmp(&alias, alias_before, sizeof(alias)) == 0);

    request = make_request(asset.handle);
    emit_invalid_trace = true;
    CHECK(!api->TraceTransformed(&request, &output));
    CHECK(memcmp(&output, &before, sizeof(output)) == 0);
    CHECK(trace_capture.calls == calls + 1u);
    emit_invalid_trace = false;

    ++sv.worr_snapshot_epoch;
    CHECK(!api->TraceTransformed(&request, &output));
    CHECK(memcmp(&output, &before, sizeof(output)) == 0);
    asset = resolve_asset(api, 2);
    CHECK(asset.handle.map_epoch == sv.worr_snapshot_epoch);
    CHECK(asset.handle.asset_hash != request.asset.asset_hash);
    request = make_request(asset.handle);
    CHECK(api->TraceTransformed(&request, &output));
}

int main(void)
{
    reset_fixture();
    test_map_and_asset_resolution();
    reset_fixture();
    test_explicit_transformed_trace();
    reset_fixture();
    test_trace_rejections_are_transactional();
    puts("server_rewind_collision_test: ok");
    return 0;
}
