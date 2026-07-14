/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "bg_local.hpp"
#include "shared/prediction_abi.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {

constexpr uint32_t kContentsSolid = UINT32_C(1) << 0;
constexpr uint32_t kContentsWater = UINT32_C(1) << 5;
constexpr uint32_t kContentsLadder = UINT32_C(1) << 29;
constexpr uint32_t kContentsPlayer = UINT32_C(1) << 30;
constexpr uint32_t kWorldEntity = 0;
constexpr uint16_t kPmfOnGround = UINT16_C(1) << 2;
constexpr uint16_t kPmfIgnorePlayerCollision = UINT16_C(1) << 9;
constexpr uint16_t kPmfHaste = UINT16_C(1) << 11;
constexpr int32_t kPmNormal = 0;
constexpr int32_t kPmGrapple = 1;
constexpr int32_t kPmSpectator = 3;
constexpr size_t kCommandRingSize = 16;
constexpr size_t kCoverageCaseCount = 27;
constexpr size_t kFailClosedCaseCount = 20;
constexpr size_t kFocusedFixtureCount = 6;

static_assert((kCommandRingSize & (kCommandRingSize - 1)) == 0);

void require(bool condition, const char *message);

struct plane_solid_t {
    std::array<float, 3> normal{};
    float distance = 0.0f;
    uint32_t surface_id = 0;
    uint32_t contents = kContentsSolid;
    uint32_t surface_flags = 0;
    uint32_t entity_id = kWorldEntity;
};

struct box_solid_t {
    std::array<float, 3> mins{};
    std::array<float, 3> maxs{};
    uint32_t surface_id = 0;
    uint32_t contents = kContentsSolid;
    uint32_t surface_flags = 0;
    uint32_t entity_id = kWorldEntity;
};

struct contents_volume_t {
    std::array<float, 3> mins{};
    std::array<float, 3> maxs{};
    uint32_t contents = 0;
};

struct collision_fixture_t {
    std::array<plane_solid_t, 3> planes{};
    size_t plane_count = 0;
    std::array<box_solid_t, 2> boxes{};
    size_t box_count = 0;
    std::array<contents_volume_t, 2> volumes{};
    size_t volume_count = 0;
    uint64_t callback_count = 0;
    uint64_t trace_callback_count = 0;
    uint64_t point_callback_count = 0;
    bool saw_start_solid = false;
    bool saw_all_solid = false;
    bool saw_player_pass = false;
    bool saw_world_only = false;
    bool saw_player_contents_mask = false;
    bool saw_mask_without_player = false;
    uint32_t dual_surface_return_count = 0;
    uint32_t expected_player_id = WORR_PREDICTION_NO_ENTITY;
    uint32_t dual_surface_primary_id = WORR_PREDICTION_NO_ENTITY;
    std::array<float, 3> dual_surface_normal{};
    float dual_surface_distance = 0.0f;
    uint32_t dual_surface_id = WORR_PREDICTION_NO_ENTITY;
    uint32_t dual_surface_flags = 0;
};

struct native_collision_context_t {
    collision_fixture_t *fixture = nullptr;
    std::array<csurface_t, 8> surfaces{};
    std::array<uint32_t, 8> surface_ids{};
    size_t surface_count = 0;
    uint32_t collision_query_count = 0;
    uint32_t presentation_query_count = 0;
    usercmd_t command_input{};
    pm_config_t config_input{};
    bool snap_initial_input = false;
    uint32_t player_entity_id_input = WORR_PREDICTION_NO_ENTITY;
    Vector3 view_offset_input{};
};

struct collision_hit_t {
    bool hit = false;
    bool start_solid = false;
    bool all_solid = false;
    float fraction = 1.0f;
    std::array<float, 3> normal{};
    float distance = 0.0f;
    uint32_t surface_id = WORR_PREDICTION_NO_ENTITY;
    uint32_t contents = 0;
    uint32_t surface_flags = 0;
    uint32_t entity_id = WORR_PREDICTION_NO_ENTITY;
};

struct scenario_spec_t {
    const char *name = nullptr;
    collision_fixture_t fixture{};
    worr_prediction_state_v1 initial_state{};
    uint32_t command_count = 0;
    float forward_move = 0.0f;
    float side_move = 0.0f;
};

struct scenario_result_t {
    const char *name = nullptr;
    uint32_t command_count = 0;
    uint64_t command_hash = 0;
    uint64_t state_hash = 0;
    uint64_t state_transcript_hash = 0;
    uint64_t collision_transcript_hash = 0;
    uint64_t replay_chain_hash = 0;
    uint64_t native_server_state_hash = 0;
    uint64_t collision_queries = 0;
    float initial_x = 0.0f;
    float initial_z = 0.0f;
    float final_x = 0.0f;
    float final_z = 0.0f;
    float maximum_z = 0.0f;
    uint16_t final_flags = 0;
    bool full_output_parity = false;
};

thread_local native_collision_context_t *active_native_collision = nullptr;

class native_collision_scope_t {
public:
    explicit native_collision_scope_t(native_collision_context_t *context)
        : previous_(active_native_collision)
    {
        require(context && !active_native_collision,
                "native collision context is already active");
        active_native_collision = context;
    }

    ~native_collision_scope_t()
    {
        active_native_collision = previous_;
    }

    native_collision_scope_t(const native_collision_scope_t &) = delete;
    native_collision_scope_t &operator=(const native_collision_scope_t &) =
        delete;

private:
    native_collision_context_t *previous_ = nullptr;
};

struct hash_contract_result_t {
    uint64_t state_hash = 0;
    uint64_t command_hash = 0;
    uint64_t config_hash = 0;
};

struct correction_result_t {
    uint32_t first_sequence = 0;
    uint32_t acknowledged_sequence = 0;
    uint32_t current_sequence = 0;
    uint32_t commands = 0;
    uint32_t replayed_commands = 0;
    uint64_t pre_correction_hash = 0;
    uint64_t authoritative_hash = 0;
    uint64_t final_hash = 0;
    uint64_t sequence_hash = 0;
    uint64_t replay_collision_hash = 0;
    uint64_t replay_chain_hash = 0;
};

struct nested_step_result_t {
    uint64_t outer_state_hash = 0;
    uint64_t outer_collision_hash = 0;
    uint64_t inner_state_hash = 0;
    uint64_t inner_collision_hash = 0;
};

struct coverage_spec_t {
    const char *name = nullptr;
    const char *category = nullptr;
    collision_fixture_t fixture{};
    worr_prediction_state_v1 initial_state{};
    worr_prediction_config_v1 config{};
    worr_prediction_command_v1 command{};
    uint16_t required_movement_flags = 0;
    uint8_t minimum_water_level = 0;
    bool require_upward_velocity = false;
};

struct coverage_result_t {
    const char *name = nullptr;
    const char *category = nullptr;
    uint64_t command_hash = 0;
    uint64_t config_hash = 0;
    uint64_t state_hash = 0;
    uint64_t native_server_state_hash = 0;
    uint64_t collision_hash = 0;
    uint32_t collision_queries = 0;
    bool full_output_parity = false;
};

struct fail_closed_result_t {
    uint32_t passed_cases = 0;
    std::array<const char *, kFailClosedCaseCount> case_names{};
    uint64_t transcript_hash = 0;
};

struct focused_fixture_result_t {
    const char *name = nullptr;
    const char *category = nullptr;
    worr_prediction_state_v1 initial_state{};
    worr_prediction_command_v1 canonical_command{};
    worr_prediction_config_v1 config{};
    uint8_t snap_initial = 0;
    uint32_t player_entity_id = WORR_PREDICTION_NO_ENTITY;
    worr_prediction_step_v1 output{};
    worr_prediction_step_v1 native_output{};
    bool full_output_equal = false;

    bool has_control = false;
    worr_prediction_state_v1 control_initial_state{};
    worr_prediction_command_v1 control_command{};
    worr_prediction_config_v1 control_config{};
    uint8_t control_snap_initial = 0;
    worr_prediction_step_v1 control_output{};
    worr_prediction_step_v1 native_control_output{};
    bool control_full_output_equal = false;

    std::array<const char *, 20> behaviour_assertions{};
    size_t behaviour_assertion_count = 0;
};

[[noreturn]] void fail(const char *message)
{
    std::fprintf(stderr, "prediction parity test failed: %s\n", message);
    std::exit(EXIT_FAILURE);
}

void require(bool condition, const char *message)
{
    if (!condition)
        fail(message);
}

float dot(const float a[3], const std::array<float, 3> &b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

float support_min(const float mins[3], const float maxs[3],
                  const std::array<float, 3> &normal)
{
    float support = 0.0f;
    for (size_t i = 0; i < 3; ++i)
        support += normal[i] * (normal[i] >= 0.0f ? mins[i] : maxs[i]);
    return support;
}

bool point_inside_box(const float point[3], const float mins[3],
                      const float maxs[3])
{
    return point[0] > mins[0] && point[0] < maxs[0] &&
           point[1] > mins[1] && point[1] < maxs[1] &&
           point[2] > mins[2] && point[2] < maxs[2];
}

collision_hit_t trace_plane(const plane_solid_t &plane,
                            const float start[3], const float mins[3],
                            const float maxs[3], const float end[3])
{
    collision_hit_t hit{};
    hit.normal = plane.normal;
    hit.surface_id = plane.surface_id;
    hit.contents = plane.contents;
    hit.surface_flags = plane.surface_flags;
    hit.entity_id = plane.entity_id;

    const float expanded_distance =
        plane.distance - support_min(mins, maxs, plane.normal);
    const float start_distance = dot(start, plane.normal) - expanded_distance;
    const float end_distance = dot(end, plane.normal) - expanded_distance;
    hit.distance = plane.distance;
    hit.start_solid = start_distance < 0.0f;
    hit.all_solid = hit.start_solid && end_distance < 0.0f;

    if (hit.all_solid) {
        hit.hit = true;
        hit.fraction = 0.0f;
    } else if (start_distance >= 0.0f && end_distance < 0.0f) {
        hit.hit = true;
        hit.fraction = start_distance / (start_distance - end_distance);
    }
    return hit;
}

collision_hit_t trace_box(const box_solid_t &box, const float start[3],
                          const float mins[3], const float maxs[3],
                          const float end[3])
{
    float expanded_mins[3];
    float expanded_maxs[3];
    for (size_t i = 0; i < 3; ++i) {
        expanded_mins[i] = box.mins[i] - maxs[i];
        expanded_maxs[i] = box.maxs[i] - mins[i];
    }

    collision_hit_t hit{};
    hit.surface_id = box.surface_id;
    hit.contents = box.contents;
    hit.surface_flags = box.surface_flags;
    hit.entity_id = box.entity_id;
    hit.start_solid = point_inside_box(start, expanded_mins, expanded_maxs);
    hit.all_solid =
        hit.start_solid && point_inside_box(end, expanded_mins, expanded_maxs);
    if (hit.all_solid) {
        hit.hit = true;
        hit.fraction = 0.0f;
        return hit;
    }
    if (hit.start_solid)
        return hit;

    float entry = -std::numeric_limits<float>::infinity();
    float exit = std::numeric_limits<float>::infinity();
    std::array<float, 3> entry_normal{};
    for (size_t i = 0; i < 3; ++i) {
        const float delta = end[i] - start[i];
        if (delta == 0.0f) {
            if (start[i] < expanded_mins[i] ||
                start[i] > expanded_maxs[i]) {
                return hit;
            }
            continue;
        }

        float near_time;
        float far_time;
        float normal_sign;
        if (delta > 0.0f) {
            near_time = (expanded_mins[i] - start[i]) / delta;
            far_time = (expanded_maxs[i] - start[i]) / delta;
            normal_sign = -1.0f;
        } else {
            near_time = (expanded_maxs[i] - start[i]) / delta;
            far_time = (expanded_mins[i] - start[i]) / delta;
            normal_sign = 1.0f;
        }
        if (near_time > entry) {
            entry = near_time;
            entry_normal = {};
            entry_normal[i] = normal_sign;
        }
        exit = std::min(exit, far_time);
        if (entry > exit)
            return hit;
    }

    if (entry < 0.0f || entry > 1.0f || exit < 0.0f)
        return hit;

    hit.hit = true;
    hit.fraction = entry;
    hit.normal = entry_normal;
    size_t normal_axis = 0;
    while (normal_axis < 3 && hit.normal[normal_axis] == 0.0f)
        ++normal_axis;
    if (normal_axis < 3) {
        const float boundary = hit.normal[normal_axis] > 0.0f
                                   ? box.maxs[normal_axis]
                                   : box.mins[normal_axis];
        hit.distance = hit.normal[normal_axis] * boundary;
    }
    return hit;
}

uint8_t plane_type(const std::array<float, 3> &normal)
{
    if (normal[0] == 1.0f || normal[0] == -1.0f)
        return 0;
    if (normal[1] == 1.0f || normal[1] == -1.0f)
        return 1;
    if (normal[2] == 1.0f || normal[2] == -1.0f)
        return 2;
    return 3;
}

uint8_t plane_sign_bits(const std::array<float, 3> &normal)
{
    uint8_t bits = 0;
    for (uint8_t i = 0; i < 3; ++i) {
        if (normal[i] < 0.0f)
            bits |= static_cast<uint8_t>(1u << i);
    }
    return bits;
}

void prediction_trace(void *context, worr_prediction_trace_v1 *result,
                      const float start[3], const float mins[3],
                      const float maxs[3], const float end[3],
                      uint32_t pass_entity_id, uint32_t contents_mask,
                      uint32_t query_flags)
{
    auto &fixture = *static_cast<collision_fixture_t *>(context);
    ++fixture.callback_count;
    ++fixture.trace_callback_count;
    fixture.saw_player_pass = fixture.saw_player_pass ||
        (fixture.expected_player_id != WORR_PREDICTION_NO_ENTITY &&
         pass_entity_id == fixture.expected_player_id);
    fixture.saw_world_only = fixture.saw_world_only ||
        ((query_flags & WORR_PREDICTION_TRACE_WORLD_ONLY) != 0 &&
         pass_entity_id == WORR_PREDICTION_NO_ENTITY);
    fixture.saw_player_contents_mask = fixture.saw_player_contents_mask ||
        ((contents_mask & kContentsPlayer) != 0);
    fixture.saw_mask_without_player = fixture.saw_mask_without_player ||
        ((contents_mask & kContentsPlayer) == 0);

    const uint32_t result_size = result->struct_size;
    const uint32_t result_version = result->schema_version;
    *result = {};
    result->struct_size = result_size;
    result->schema_version = result_version;
    result->fraction = 1.0f;
    std::copy_n(end, 3, result->end);
    result->surface_id = WORR_PREDICTION_NO_ENTITY;
    result->surface2_id = WORR_PREDICTION_NO_ENTITY;
    result->entity_id = WORR_PREDICTION_NO_ENTITY;

    collision_hit_t best{};
    bool start_solid = false;
    bool all_solid = false;
    auto consider = [&](const collision_hit_t &candidate) {
        start_solid = start_solid || candidate.start_solid;
        all_solid = all_solid || candidate.all_solid;
        if (candidate.hit &&
            (!best.hit || candidate.fraction < best.fraction)) {
            best = candidate;
        }
    };

    for (size_t i = 0; i < fixture.plane_count; ++i) {
        if (contents_mask & fixture.planes[i].contents) {
            consider(trace_plane(fixture.planes[i], start, mins, maxs,
                                 end));
        }
    }
    for (size_t i = 0; i < fixture.box_count; ++i) {
        if (contents_mask & fixture.boxes[i].contents) {
            consider(trace_box(fixture.boxes[i], start, mins, maxs, end));
        }
    }

    result->start_solid = start_solid ? 1 : 0;
    result->all_solid = all_solid ? 1 : 0;
    fixture.saw_start_solid = fixture.saw_start_solid || start_solid;
    fixture.saw_all_solid = fixture.saw_all_solid || all_solid;
    if (!best.hit)
        return;

    result->fraction = std::clamp(best.fraction, 0.0f, 1.0f);
    for (size_t i = 0; i < 3; ++i) {
        result->end[i] =
            start[i] + (end[i] - start[i]) * result->fraction;
        result->plane.normal[i] = best.normal[i];
    }
    result->plane.distance = best.distance;
    result->plane.type = plane_type(best.normal);
    result->plane.sign_bits = plane_sign_bits(best.normal);
    result->surface_id = best.surface_id;
    result->surface_flags = best.surface_flags;
    result->contents = best.contents;
    result->entity_id = best.entity_id;
    if (best.surface_id == fixture.dual_surface_primary_id) {
        ++fixture.dual_surface_return_count;
        result->has_second_surface = 1;
        std::copy(fixture.dual_surface_normal.begin(),
                  fixture.dual_surface_normal.end(),
                  result->plane2.normal);
        result->plane2.distance = fixture.dual_surface_distance;
        result->plane2.type = plane_type(fixture.dual_surface_normal);
        result->plane2.sign_bits =
            plane_sign_bits(fixture.dual_surface_normal);
        result->surface2_id = fixture.dual_surface_id;
        result->surface2_flags = fixture.dual_surface_flags;
    }
}

uint32_t prediction_point_contents(void *context, const float point[3])
{
    auto &fixture = *static_cast<collision_fixture_t *>(context);
    ++fixture.callback_count;
    ++fixture.point_callback_count;

    uint32_t contents = 0;
    for (size_t i = 0; i < fixture.plane_count; ++i) {
        if (dot(point, fixture.planes[i].normal) <
            fixture.planes[i].distance) {
            contents |= fixture.planes[i].contents;
        }
    }
    for (size_t i = 0; i < fixture.box_count; ++i) {
        if (point_inside_box(point, fixture.boxes[i].mins.data(),
                             fixture.boxes[i].maxs.data())) {
            contents |= fixture.boxes[i].contents;
        }
    }
    for (size_t i = 0; i < fixture.volume_count; ++i) {
        if (point_inside_box(point, fixture.volumes[i].mins.data(),
                             fixture.volumes[i].maxs.data())) {
            contents |= fixture.volumes[i].contents;
        }
    }
    return contents;
}

gentity_t *native_entity_token(uint32_t entity_id)
{
    if (entity_id == WORR_PREDICTION_NO_ENTITY)
        return nullptr;
    return reinterpret_cast<gentity_t *>(
        static_cast<uintptr_t>(entity_id) + 1u);
}

uint32_t native_entity_id(const gentity_t *entity)
{
    if (!entity)
        return WORR_PREDICTION_NO_ENTITY;
    const uintptr_t token = reinterpret_cast<uintptr_t>(entity);
    require(token > 0 && token - 1u < WORR_PREDICTION_NO_ENTITY,
            "native collision callback received an invalid entity token");
    return static_cast<uint32_t>(token - 1u);
}

csurface_t *native_surface(uint32_t surface_id, uint32_t surface_flags)
{
    if (surface_id == WORR_PREDICTION_NO_ENTITY)
        return nullptr;
    require(active_native_collision != nullptr,
            "native surface requested without a collision context");

    for (size_t i = 0; i < active_native_collision->surface_count; ++i) {
        if (active_native_collision->surface_ids[i] != surface_id)
            continue;
        require(static_cast<uint32_t>(
                    active_native_collision->surfaces[i].flags) ==
                    surface_flags,
                "native surface ID changed flags during a move");
        return &active_native_collision->surfaces[i];
    }

    require(active_native_collision->surface_count <
                active_native_collision->surfaces.size(),
            "native analytic surface capacity exceeded");
    const size_t index = active_native_collision->surface_count++;
    active_native_collision->surface_ids[index] = surface_id;
    csurface_t &surface = active_native_collision->surfaces[index];
    surface = {};
    surface.id = surface_id;
    surface.flags = static_cast<surfflags_t>(surface_flags);
    return &surface;
}

trace_t native_trace_common(gvec3_cref_t start, gvec3_cptr_t mins,
                            gvec3_cptr_t maxs, gvec3_cref_t end,
                            const gentity_t *pass_entity,
                            contents_t contents_mask, uint32_t query_flags)
{
    require(active_native_collision && active_native_collision->fixture,
            "native trace called without a collision context");
    ++active_native_collision->collision_query_count;

    const Vector3 zero{};
    const Vector3 &query_mins = mins ? *mins : zero;
    const Vector3 &query_maxs = maxs ? *maxs : zero;
    worr_prediction_trace_v1 result{};
    result.struct_size = sizeof(result);
    result.schema_version = WORR_PREDICTION_ABI_VERSION;
    result.fraction = 1.0f;
    result.surface_id = WORR_PREDICTION_NO_ENTITY;
    result.surface2_id = WORR_PREDICTION_NO_ENTITY;
    result.entity_id = WORR_PREDICTION_NO_ENTITY;
    prediction_trace(active_native_collision->fixture, &result, start.data(),
                     query_mins.data(), query_maxs.data(), end.data(),
                     native_entity_id(pass_entity),
                     static_cast<uint32_t>(contents_mask), query_flags);

    trace_t trace{};
    trace.allSolid = result.all_solid != 0;
    trace.startSolid = result.start_solid != 0;
    trace.fraction = result.fraction;
    trace.endPos = {result.end[0], result.end[1], result.end[2]};
    trace.plane.normal = {result.plane.normal[0], result.plane.normal[1],
                          result.plane.normal[2]};
    trace.plane.dist = result.plane.distance;
    trace.plane.type = result.plane.type;
    trace.plane.signBits = result.plane.sign_bits;
    trace.surface = native_surface(result.surface_id, result.surface_flags);
    trace.contents = static_cast<contents_t>(result.contents);
    trace.ent = native_entity_token(result.entity_id);
    trace.plane2.normal = {result.plane2.normal[0], result.plane2.normal[1],
                           result.plane2.normal[2]};
    trace.plane2.dist = result.plane2.distance;
    trace.plane2.type = result.plane2.type;
    trace.plane2.signBits = result.plane2.sign_bits;
    if (result.has_second_surface) {
        trace.surface2 =
            native_surface(result.surface2_id, result.surface2_flags);
    }
    return trace;
}

trace_t native_trace(gvec3_cref_t start, gvec3_cptr_t mins,
                     gvec3_cptr_t maxs, gvec3_cref_t end,
                     const gentity_t *pass_entity, contents_t contents_mask)
{
    return native_trace_common(start, mins, maxs, end, pass_entity,
                               contents_mask, 0);
}

trace_t native_clip(gvec3_cref_t start, gvec3_cptr_t mins,
                    gvec3_cptr_t maxs, gvec3_cref_t end,
                    contents_t contents_mask)
{
    return native_trace_common(start, mins, maxs, end, nullptr,
                               contents_mask,
                               WORR_PREDICTION_TRACE_WORLD_ONLY);
}

contents_t native_point_contents(gvec3_cref_t point)
{
    require(active_native_collision && active_native_collision->fixture,
            "native point-contents called without a collision context");
    ++active_native_collision->collision_query_count;
    return static_cast<contents_t>(prediction_point_contents(
        active_native_collision->fixture, point.data()));
}

contents_t native_presentation_point_contents(gvec3_cref_t point)
{
    require(active_native_collision && active_native_collision->fixture,
            "native presentation point-contents called without a collision context");
    ++active_native_collision->presentation_query_count;
    return static_cast<contents_t>(prediction_point_contents(
        active_native_collision->fixture, point.data()));
}

uint64_t append_u64(uint64_t hash, uint64_t value)
{
    for (unsigned i = 0; i < 8; ++i) {
        hash ^= static_cast<uint8_t>(value >> (i * 8));
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

uint64_t append_string(uint64_t hash, std::string_view value)
{
    hash = append_u64(hash, value.size());
    for (const char character : value) {
        hash ^= static_cast<uint8_t>(character);
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

uint64_t begin_transcript()
{
    return UINT64_C(14695981039346656037);
}

worr_prediction_state_v1 make_state(float x, float y, float z)
{
    worr_prediction_state_v1 state{};
    state.struct_size = sizeof(state);
    state.schema_version = WORR_PREDICTION_ABI_VERSION;
    state.movement_type = kPmNormal;
    state.origin[0] = x;
    state.origin[1] = y;
    state.origin[2] = z;
    state.gravity = 800;
    state.view_height = 22;
    return state;
}

worr_prediction_config_v1 make_config()
{
    worr_prediction_config_v1 config{};
    config.struct_size = sizeof(config);
    config.schema_version = WORR_PREDICTION_ABI_VERSION;
    config.movement_model_revision = WORR_PREDICTION_MODEL_REVISION;
    config.air_acceleration = 1;
    return config;
}

worr_prediction_command_v1 make_command(uint32_t ordinal, float forward,
                                        float side)
{
    worr_prediction_command_v1 command{};
    command.struct_size = sizeof(command);
    command.schema_version = WORR_PREDICTION_ABI_VERSION;
    command.duration_ms = static_cast<uint8_t>(16 + (ordinal % 3 == 1));
    command.view_angles[0] = 0.0f;
    command.view_angles[1] = 0.0f;
    command.view_angles[2] = 0.0f;
    command.forward_move = forward + static_cast<float>((ordinal % 5) * 4);
    command.side_move = (ordinal & 1u) ? -side : side;
    return command;
}

worr_prediction_step_v1 make_step(
    const worr_prediction_state_v1 &state,
    const worr_prediction_config_v1 &config, collision_fixture_t *fixture)
{
    worr_prediction_step_v1 step{};
    step.struct_size = sizeof(step);
    step.schema_version = WORR_PREDICTION_ABI_VERSION;
    step.state = state;
    step.command = make_command(0, 0.0f, 0.0f);
    step.config = config;
    step.player_entity_id = 7;
    step.collision_context = fixture;
    step.trace = prediction_trace;
    step.point_contents = prediction_point_contents;
    return step;
}

pmove_state_t native_state_from_abi(
    const worr_prediction_state_v1 &state)
{
    pmove_state_t native{};
    native.pmType = static_cast<pmtype_t>(state.movement_type);
    native.origin = {state.origin[0], state.origin[1], state.origin[2]};
    native.velocity =
        {state.velocity[0], state.velocity[1], state.velocity[2]};
    native.pmFlags = static_cast<pmflags_t>(state.movement_flags);
    native.pmTime = state.movement_time_ms;
    native.gravity = state.gravity;
    native.viewHeight = state.view_height;
    native.deltaAngles = {state.delta_angles[0], state.delta_angles[1],
                          state.delta_angles[2]};
    return native;
}

worr_prediction_state_v1 abi_state_from_native(
    const pmove_state_t &state)
{
    worr_prediction_state_v1 abi{};
    abi.struct_size = sizeof(abi);
    abi.schema_version = WORR_PREDICTION_ABI_VERSION;
    abi.movement_type = static_cast<int32_t>(state.pmType);
    std::copy_n(state.origin.data(), 3, abi.origin);
    std::copy_n(state.velocity.data(), 3, abi.velocity);
    abi.movement_flags = static_cast<uint16_t>(state.pmFlags);
    abi.movement_time_ms = state.pmTime;
    abi.gravity = state.gravity;
    abi.view_height = state.viewHeight;
    std::copy_n(state.deltaAngles.data(), 3, abi.delta_angles);
    return abi;
}

usercmd_t native_command_from_abi(
    const worr_prediction_command_v1 &command)
{
    usercmd_t native{};
    native.msec = command.duration_ms;
    native.buttons = static_cast<button_t>(command.buttons);
    native.angles = {command.view_angles[0], command.view_angles[1],
                     command.view_angles[2]};
    native.forwardMove = command.forward_move;
    native.sideMove = command.side_move;
    return native;
}

PMove make_native_move(const worr_prediction_state_v1 &state,
                       const worr_prediction_config_v1 &config)
{
    PMove move{};
    move.s = native_state_from_abi(state);
    move.config.airAccel = config.air_acceleration;
    move.config.n64Physics =
        (config.flags & WORR_PREDICTION_CONFIG_N64_PHYSICS) != 0;
    move.config.q3Overbounce =
        (config.flags & WORR_PREDICTION_CONFIG_Q3_OVERBOUNCE) != 0;
    move.player = native_entity_token(7);
    move.trace = native_trace;
    move.clip = native_clip;
    move.pointContents = native_point_contents;
    move.presentationPointContents = native_presentation_point_contents;
    return move;
}

void configure_native_step_inputs(PMove &move,
                                  const worr_prediction_step_v1 &step)
{
    move.snapInitial = step.snap_initial != 0;
    move.player = native_entity_token(step.player_entity_id);
    move.viewOffset = {step.view_offset[0], step.view_offset[1],
                       step.view_offset[2]};
}

void run_native_move(PMove &move, native_collision_context_t &context,
                     const worr_prediction_command_v1 &command)
{
    move.cmd = native_command_from_abi(command);
    context.collision_query_count = 0;
    context.presentation_query_count = 0;
    context.command_input = move.cmd;
    context.config_input = move.config;
    context.snap_initial_input = move.snapInitial;
    context.player_entity_id_input = native_entity_id(move.player);
    context.view_offset_input = move.viewOffset;
    native_collision_scope_t scope(&context);
    Pmove(&move);
}

collision_fixture_t floor_fixture();
collision_fixture_t wall_fixture();
bool equal_state(const worr_prediction_state_v1 &left,
                 const worr_prediction_state_v1 &right);
bool equal_full_output_values(const worr_prediction_step_v1 &left,
                              const worr_prediction_step_v1 &right,
                              bool compare_collision_hash);

struct nested_collision_context_t {
    collision_fixture_t outer_fixture{};
    worr_prediction_step_v1 inner_step{};
    bool invoked = false;
    bool inner_succeeded = false;
};

void nested_prediction_trace(void *context,
                             worr_prediction_trace_v1 *result,
                             const float start[3], const float mins[3],
                             const float maxs[3], const float end[3],
                             uint32_t pass_entity_id,
                             uint32_t contents_mask, uint32_t query_flags)
{
    auto &nested = *static_cast<nested_collision_context_t *>(context);
    if (!nested.invoked) {
        nested.invoked = true;
        nested.inner_succeeded = Worr_PredictionStepV1(&nested.inner_step);
    }
    prediction_trace(&nested.outer_fixture, result, start, mins, maxs, end,
                     pass_entity_id, contents_mask, query_flags);
}

uint32_t nested_prediction_point_contents(void *context,
                                          const float point[3])
{
    auto &nested = *static_cast<nested_collision_context_t *>(context);
    return prediction_point_contents(&nested.outer_fixture, point);
}

nested_step_result_t validate_nested_step()
{
    const worr_prediction_config_v1 config = make_config();
    worr_prediction_command_v1 outer_command =
        make_command(0, 220.0f, 0.0f);
    worr_prediction_command_v1 inner_command =
        make_command(1, 180.0f, 20.0f);
    require(Worr_PredictionCanonicalizeCommandV1(&outer_command) &&
                Worr_PredictionCanonicalizeCommandV1(&inner_command),
            "nested-step command canonicalization failed");

    collision_fixture_t outer_control_fixture = floor_fixture();
    worr_prediction_step_v1 outer_control = make_step(
        make_state(0.0f, 0.0f, 24.0f), config, &outer_control_fixture);
    outer_control.command = outer_command;

    collision_fixture_t inner_fixture = wall_fixture();
    nested_collision_context_t nested{};
    nested.outer_fixture = floor_fixture();
    nested.inner_step = make_step(make_state(8.0f, 0.0f, 24.0f), config,
                                  &inner_fixture);
    nested.inner_step.command = inner_command;

    collision_fixture_t inner_control_fixture = wall_fixture();
    worr_prediction_step_v1 inner_control = make_step(
        make_state(8.0f, 0.0f, 24.0f), config,
        &inner_control_fixture);
    inner_control.command = inner_command;

    worr_prediction_step_v1 outer_nested = make_step(
        make_state(0.0f, 0.0f, 24.0f), config,
        &nested.outer_fixture);
    outer_nested.collision_context = &nested;
    outer_nested.command = outer_command;
    outer_nested.trace = nested_prediction_trace;
    outer_nested.point_contents = nested_prediction_point_contents;

    require(Worr_PredictionStepV1(&outer_nested),
            "outer nested prediction step failed");
    require(nested.invoked && nested.inner_succeeded,
            "outer trace did not complete its nested prediction step");
    require(Worr_PredictionStepV1(&outer_control),
            "outer control prediction step failed");
    require(Worr_PredictionStepV1(&inner_control),
            "inner control prediction step failed");
    require(equal_full_output_values(outer_nested, outer_control, true),
            "nested invocation did not restore the outer prediction scope");
    require(equal_full_output_values(nested.inner_step, inner_control, true),
            "nested prediction step diverged from its independent control");

    return {outer_nested.state_hash, outer_nested.collision_hash,
            nested.inner_step.state_hash, nested.inner_step.collision_hash};
}

enum class malformed_trace_mode_t {
    non_finite_fraction,
    surface_without_id,
    invalid_struct_size,
    conflicting_surface_flags,
};

struct malformed_trace_context_t {
    collision_fixture_t fixture{};
    malformed_trace_mode_t mode = malformed_trace_mode_t::non_finite_fraction;
    uint32_t surface_hit_count = 0;
};

void malformed_prediction_trace(void *context,
                                worr_prediction_trace_v1 *result,
                                const float start[3], const float mins[3],
                                const float maxs[3], const float end[3],
                                uint32_t pass_entity_id,
                                uint32_t contents_mask,
                                uint32_t query_flags)
{
    auto &malformed = *static_cast<malformed_trace_context_t *>(context);
    prediction_trace(&malformed.fixture, result, start, mins, maxs, end,
                     pass_entity_id, contents_mask, query_flags);
    switch (malformed.mode) {
    case malformed_trace_mode_t::non_finite_fraction:
        result->fraction = std::numeric_limits<float>::quiet_NaN();
        break;
    case malformed_trace_mode_t::surface_without_id:
        result->surface_id = WORR_PREDICTION_NO_ENTITY;
        result->surface_flags = 1;
        break;
    case malformed_trace_mode_t::invalid_struct_size:
        --result->struct_size;
        break;
    case malformed_trace_mode_t::conflicting_surface_flags:
        if (result->surface_id != WORR_PREDICTION_NO_ENTITY) {
            result->surface_flags = malformed.surface_hit_count++ == 0
                ? 0u
                : 1u;
        }
        break;
    }
}

uint32_t malformed_prediction_point_contents(void *context,
                                             const float point[3])
{
    auto &malformed = *static_cast<malformed_trace_context_t *>(context);
    return prediction_point_contents(&malformed.fixture, point);
}

void throwing_prediction_trace(void *, worr_prediction_trace_v1 *,
                               const float[3], const float[3],
                               const float[3], const float[3], uint32_t,
                               uint32_t, uint32_t)
{
    throw std::runtime_error("intentional prediction callback failure");
}

bool equal_float_bits(float left, float right)
{
    return std::bit_cast<uint32_t>(left) == std::bit_cast<uint32_t>(right);
}

bool equal_state(const worr_prediction_state_v1 &left,
                 const worr_prediction_state_v1 &right)
{
    if (left.struct_size != right.struct_size ||
        left.schema_version != right.schema_version ||
        left.movement_type != right.movement_type ||
        left.movement_flags != right.movement_flags ||
        left.movement_time_ms != right.movement_time_ms ||
        left.gravity != right.gravity || left.view_height != right.view_height)
        return false;
    for (size_t i = 0; i < 3; ++i) {
        if (!equal_float_bits(left.origin[i], right.origin[i]) ||
            !equal_float_bits(left.velocity[i], right.velocity[i]) ||
            !equal_float_bits(left.delta_angles[i], right.delta_angles[i])) {
            return false;
        }
    }
    return true;
}

bool equal_command(const worr_prediction_command_v1 &left,
                   const worr_prediction_command_v1 &right)
{
    if (left.struct_size != right.struct_size ||
        left.schema_version != right.schema_version ||
        left.duration_ms != right.duration_ms ||
        left.buttons != right.buttons || left.reserved0 != right.reserved0) {
        return false;
    }
    for (size_t i = 0; i < 3; ++i) {
        if (!equal_float_bits(left.view_angles[i], right.view_angles[i]))
            return false;
    }
    return equal_float_bits(left.forward_move, right.forward_move) &&
           equal_float_bits(left.side_move, right.side_move);
}

bool equal_config(const worr_prediction_config_v1 &left,
                  const worr_prediction_config_v1 &right)
{
    return left.struct_size == right.struct_size &&
           left.schema_version == right.schema_version &&
           left.movement_model_revision == right.movement_model_revision &&
           left.air_acceleration == right.air_acceleration &&
           left.flags == right.flags;
}

bool equal_vec(const float left[3], const float right[3])
{
    for (size_t i = 0; i < 3; ++i) {
        if (!equal_float_bits(left[i], right[i]))
            return false;
    }
    return true;
}

bool equal_plane(const worr_prediction_plane_v1 &left,
                 const worr_prediction_plane_v1 &right)
{
    return equal_vec(left.normal, right.normal) &&
           equal_float_bits(left.distance, right.distance) &&
           left.type == right.type && left.sign_bits == right.sign_bits &&
           left.reserved[0] == right.reserved[0] &&
           left.reserved[1] == right.reserved[1];
}

worr_prediction_step_v1 native_output_snapshot(
    const worr_prediction_step_v1 &input, const PMove &native,
    const native_collision_context_t &context)
{
    worr_prediction_step_v1 output = input;
    output.state = abi_state_from_native(native.s);
    output.command = input.command;
    std::copy_n(native.viewAngles.data(), 3, output.view_angles);
    std::copy_n(native.mins.data(), 3, output.mins);
    std::copy_n(native.maxs.data(), 3, output.maxs);
    std::copy_n(native.groundPlane.normal.data(), 3,
                output.ground_plane.normal);
    output.ground_plane.distance = native.groundPlane.dist;
    output.ground_plane.type = native.groundPlane.type;
    output.ground_plane.sign_bits = native.groundPlane.signBits;
    output.ground_plane.reserved[0] = 0;
    output.ground_plane.reserved[1] = 0;
    output.ground_entity_id = native_entity_id(native.groundEntity);
    output.water_type = static_cast<uint32_t>(native.waterType);
    output.water_level = static_cast<uint8_t>(native.waterLevel);
    output.rd_flags = static_cast<uint8_t>(native.rdFlags);
    output.jump_sound = native.jumpSound ? 1 : 0;
    output.step_clip = native.stepClip ? 1 : 0;
    output.impact_delta = native.impactDelta;
    std::copy_n(native.screenBlend.data(), 4, output.screen_blend);
    output.touch_count =
        std::min<uint32_t>(native.touch.num, WORR_PREDICTION_MAX_TOUCH);
    for (uint32_t i = 0; i < output.touch_count; ++i) {
        output.touch_entity_ids[i] =
            native_entity_id(native.touch.traces[i].ent);
    }
    for (uint32_t i = output.touch_count;
         i < WORR_PREDICTION_MAX_TOUCH; ++i) {
        output.touch_entity_ids[i] = WORR_PREDICTION_NO_ENTITY;
    }
    output.collision_query_count = context.collision_query_count;
    output.reserved1 = 0;
    output.state_hash = Worr_PredictionHashStateV1(&output.state);
    output.collision_hash = 0;
    return output;
}

bool equal_full_output_values(const worr_prediction_step_v1 &left,
                              const worr_prediction_step_v1 &right,
                              bool compare_collision_hash)
{
    if (left.struct_size != right.struct_size ||
        left.schema_version != right.schema_version ||
        !equal_state(left.state, right.state) ||
        !equal_command(left.command, right.command) ||
        !equal_config(left.config, right.config) ||
        left.snap_initial != right.snap_initial ||
        left.reserved0[0] != right.reserved0[0] ||
        left.reserved0[1] != right.reserved0[1] ||
        left.reserved0[2] != right.reserved0[2] ||
        left.player_entity_id != right.player_entity_id ||
        !equal_vec(left.view_offset, right.view_offset) ||
        !equal_vec(left.view_angles, right.view_angles) ||
        !equal_vec(left.mins, right.mins) ||
        !equal_vec(left.maxs, right.maxs) ||
        !equal_plane(left.ground_plane, right.ground_plane) ||
        left.ground_entity_id != right.ground_entity_id ||
        left.water_type != right.water_type ||
        left.water_level != right.water_level ||
        left.rd_flags != right.rd_flags ||
        left.jump_sound != right.jump_sound ||
        left.step_clip != right.step_clip ||
        !equal_float_bits(left.impact_delta, right.impact_delta) ||
        left.touch_count != right.touch_count ||
        left.collision_query_count != right.collision_query_count ||
        left.reserved1 != right.reserved1 ||
        left.state_hash != right.state_hash ||
        (compare_collision_hash &&
         left.collision_hash != right.collision_hash)) {
        return false;
    }
    for (size_t i = 0; i < 4; ++i) {
        if (!equal_float_bits(left.screen_blend[i], right.screen_blend[i]))
            return false;
    }
    for (size_t i = 0; i < WORR_PREDICTION_MAX_TOUCH; ++i) {
        if (left.touch_entity_ids[i] != right.touch_entity_ids[i])
            return false;
        if (i >= left.touch_count &&
            left.touch_entity_ids[i] != WORR_PREDICTION_NO_ENTITY) {
            return false;
        }
    }
    return true;
}

void report_full_output_difference(const worr_prediction_step_v1 &abi,
                                   const worr_prediction_step_v1 &native)
{
    auto report = [](bool equal, const char *field) {
        if (!equal)
            std::fprintf(stderr, "  full-output mismatch: %s\n", field);
    };
    report(equal_state(abi.state, native.state), "state");
    report(equal_command(abi.command, native.command), "command");
    report(equal_config(abi.config, native.config), "config");
    report(abi.snap_initial == native.snap_initial, "snap_initial");
    report(abi.reserved0[0] == native.reserved0[0] &&
               abi.reserved0[1] == native.reserved0[1] &&
               abi.reserved0[2] == native.reserved0[2],
           "reserved0");
    report(abi.player_entity_id == native.player_entity_id,
           "player_entity_id");
    report(equal_vec(abi.view_offset, native.view_offset), "view_offset");
    report(equal_vec(abi.view_angles, native.view_angles), "view_angles");
    report(equal_vec(abi.mins, native.mins), "mins");
    report(equal_vec(abi.maxs, native.maxs), "maxs");
    report(equal_plane(abi.ground_plane, native.ground_plane),
           "ground_plane");
    report(abi.ground_entity_id == native.ground_entity_id,
           "ground_entity_id");
    report(abi.water_type == native.water_type, "water_type");
    report(abi.water_level == native.water_level, "water_level");
    report(abi.rd_flags == native.rd_flags, "rd_flags");
    report(abi.jump_sound == native.jump_sound, "jump_sound");
    report(abi.step_clip == native.step_clip, "step_clip");
    report(equal_float_bits(abi.impact_delta, native.impact_delta),
           "impact_delta");
    for (size_t i = 0; i < 4; ++i) {
        if (!equal_float_bits(abi.screen_blend[i], native.screen_blend[i]))
            std::fprintf(stderr,
                         "  full-output mismatch: screen_blend[%zu]\n", i);
    }
    report(abi.touch_count == native.touch_count, "touch_count");
    for (size_t i = 0; i < WORR_PREDICTION_MAX_TOUCH; ++i) {
        if (abi.touch_entity_ids[i] != native.touch_entity_ids[i]) {
            std::fprintf(stderr,
                         "  full-output mismatch: touch_entity_ids[%zu] "
                         "(%u != %u)\n",
                         i, abi.touch_entity_ids[i],
                         native.touch_entity_ids[i]);
        }
    }
    if (abi.collision_query_count != native.collision_query_count) {
        std::fprintf(stderr,
                     "  full-output mismatch: collision_query_count "
                     "(%u != %u)\n",
                     abi.collision_query_count,
                     native.collision_query_count);
    }
    report(abi.reserved1 == native.reserved1, "reserved1");
    report(abi.state_hash == native.state_hash, "state_hash");
}

bool equal_full_native_output(
    const worr_prediction_step_v1 &abi, const PMove &native,
    const native_collision_context_t &context,
    worr_prediction_step_v1 *native_snapshot = nullptr)
{
    worr_prediction_step_v1 snapshot =
        native_output_snapshot(abi, native, context);
    snapshot.command = abi.command;
    bool native_command_equal =
        context.command_input.msec == abi.command.duration_ms &&
        static_cast<uint8_t>(context.command_input.buttons) ==
            abi.command.buttons &&
        equal_float_bits(context.command_input.forwardMove,
                         abi.command.forward_move) &&
        equal_float_bits(context.command_input.sideMove,
                         abi.command.side_move);
    for (size_t i = 0; i < 3; ++i) {
        native_command_equal = native_command_equal &&
            equal_float_bits(context.command_input.angles[i],
                             abi.command.view_angles[i]) &&
            equal_float_bits(context.view_offset_input[i],
                             abi.view_offset[i]);
    }
    const bool native_inputs_equal = native_command_equal &&
        context.config_input.airAccel == abi.config.air_acceleration &&
        context.config_input.n64Physics ==
            ((abi.config.flags & WORR_PREDICTION_CONFIG_N64_PHYSICS) != 0) &&
        context.config_input.q3Overbounce ==
            ((abi.config.flags & WORR_PREDICTION_CONFIG_Q3_OVERBOUNCE) != 0) &&
        context.snap_initial_input == (abi.snap_initial != 0) &&
        context.player_entity_id_input == abi.player_entity_id;
    const bool equal = native_inputs_equal && abi.state_hash != 0 &&
        abi.collision_hash != 0 &&
        equal_full_output_values(abi, snapshot, false);
    if (!equal) {
        if (!native_inputs_equal)
            std::fprintf(stderr,
                         "  full-output mismatch: native simulation inputs\n");
        if (abi.state_hash == 0)
            std::fprintf(stderr, "  full-output mismatch: zero state hash\n");
        if (abi.collision_hash == 0)
            std::fprintf(stderr,
                         "  full-output mismatch: zero collision hash\n");
        report_full_output_difference(abi, snapshot);
    }
    if (native_snapshot)
        *native_snapshot = snapshot;
    return equal;
}

collision_fixture_t empty_fixture()
{
    return {};
}

collision_fixture_t floor_fixture()
{
    collision_fixture_t fixture{};
    fixture.planes[0] = {{{0.0f, 0.0f, 1.0f}}, 0.0f, 1};
    fixture.plane_count = 1;
    return fixture;
}

collision_fixture_t wall_fixture()
{
    collision_fixture_t fixture = floor_fixture();
    fixture.planes[1] = {{{-1.0f, 0.0f, 0.0f}}, -72.0f, 2};
    fixture.plane_count = 2;
    return fixture;
}

collision_fixture_t diagonal_wall_fixture()
{
    collision_fixture_t fixture = floor_fixture();
    fixture.planes[1] =
        {{{-0.707106781f, 0.707106781f, 0.0f}}, -62.2253952f, 6};
    fixture.plane_count = 2;
    return fixture;
}

collision_fixture_t step_fixture()
{
    collision_fixture_t fixture = floor_fixture();
    fixture.boxes[0] = {{{40.0f, -64.0f, 0.0f}},
                        {{56.0f, 64.0f, 12.0f}}, 3};
    fixture.box_count = 1;
    return fixture;
}

collision_fixture_t slope_fixture()
{
    collision_fixture_t fixture{};
    fixture.planes[0] =
        {{{-0.242535625f, 0.0f, 0.970142484f}}, 0.0f, 4};
    fixture.plane_count = 1;
    return fixture;
}

collision_fixture_t water_fixture()
{
    collision_fixture_t fixture = floor_fixture();
    fixture.volumes[0] = {{{-128.0f, -128.0f, -64.0f}},
                          {{128.0f, 128.0f, 40.0f}}, kContentsWater};
    fixture.volume_count = 1;
    return fixture;
}

collision_fixture_t ladder_fixture()
{
    collision_fixture_t fixture = floor_fixture();
    fixture.planes[1] = {{{-1.0f, 0.0f, 0.0f}}, -16.5f, 5,
                          kContentsSolid | kContentsLadder, 0};
    fixture.plane_count = 2;
    return fixture;
}

float slope_supported_height(float x)
{
    const collision_fixture_t fixture = slope_fixture();
    const auto &normal = fixture.planes[0].normal;
    const float mins[3] = {-16.0f, -16.0f, -24.0f};
    const float maxs[3] = {16.0f, 16.0f, 32.0f};
    const float expanded_distance = -support_min(mins, maxs, normal);
    return (expanded_distance - normal[0] * x) / normal[2];
}

std::array<scenario_spec_t, 5> make_scenarios()
{
    return {{
        {"empty", empty_fixture(), make_state(0.0f, 0.0f, 96.0f), 18,
         180.0f, 24.0f},
        {"floor", floor_fixture(), make_state(0.0f, 0.0f, 24.0f), 36,
         220.0f, 0.0f},
        {"wall", wall_fixture(), make_state(0.0f, 0.0f, 24.0f), 56,
         300.0f, 0.0f},
        {"step", step_fixture(), make_state(0.0f, 0.0f, 24.0f), 60,
         220.0f, 0.0f},
        {"slope", slope_fixture(),
         make_state(-32.0f, 0.0f, slope_supported_height(-32.0f)), 40,
         180.0f, 0.0f},
    }};
}

scenario_result_t run_scenario(const scenario_spec_t &spec)
{
    collision_fixture_t client_fixture = spec.fixture;
    collision_fixture_t bridge_repeat_fixture = spec.fixture;
    collision_fixture_t native_server_fixture = spec.fixture;
    native_collision_context_t native_server_context{};
    native_server_context.fixture = &native_server_fixture;
    const worr_prediction_config_v1 client_config = make_config();
    const worr_prediction_config_v1 bridge_repeat_config = make_config();
    const worr_prediction_config_v1 native_server_config = make_config();
    worr_prediction_step_v1 client =
        make_step(spec.initial_state, client_config, &client_fixture);
    worr_prediction_step_v1 bridge_repeat = make_step(
        spec.initial_state, bridge_repeat_config, &bridge_repeat_fixture);
    PMove native_server =
        make_native_move(spec.initial_state, native_server_config);
    configure_native_step_inputs(native_server, client);

    scenario_result_t result{};
    result.name = spec.name;
    result.command_count = spec.command_count;
    result.command_hash = begin_transcript();
    result.state_transcript_hash = begin_transcript();
    result.collision_transcript_hash = begin_transcript();
    result.replay_chain_hash = 0;
    result.initial_x = spec.initial_state.origin[0];
    result.initial_z = spec.initial_state.origin[2];
    result.maximum_z = result.initial_z;
    result.full_output_parity = true;

    for (uint32_t i = 0; i < spec.command_count; ++i) {
        worr_prediction_command_v1 client_command =
            make_command(i, spec.forward_move, spec.side_move);
        require(Worr_PredictionCanonicalizeCommandV1(&client_command),
                "scenario command canonicalization failed");
        const worr_prediction_command_v1 bridge_repeat_command =
            client_command;
        const worr_prediction_command_v1 native_server_command =
            client_command;
        const uint64_t client_command_hash =
            Worr_PredictionHashCommandV1(&client_command);
        const uint64_t bridge_repeat_command_hash =
            Worr_PredictionHashCommandV1(&bridge_repeat_command);
        require(client_command_hash != 0 &&
                    client_command_hash == bridge_repeat_command_hash,
                "ABI bridge command hash mismatch");
        result.command_hash = append_u64(result.command_hash,
                                         client_command_hash);

        client.command = client_command;
        bridge_repeat.command = bridge_repeat_command;
        const uint64_t client_callbacks_before = client_fixture.callback_count;
        const uint64_t bridge_callbacks_before =
            bridge_repeat_fixture.callback_count;
        const uint64_t native_callbacks_before =
            native_server_fixture.callback_count;
        require(Worr_PredictionStepV1(&client),
                "client ABI prediction step rejected");
        require(Worr_PredictionStepV1(&bridge_repeat),
                "repeat ABI prediction step rejected");
        run_native_move(native_server, native_server_context,
                        native_server_command);
        const worr_prediction_state_v1 native_server_state =
            abi_state_from_native(native_server.s);
        const uint64_t native_server_state_hash =
            Worr_PredictionHashStateV1(&native_server_state);

        require(equal_full_output_values(client, bridge_repeat, true),
                "independent ABI bridge full outputs diverged");
        const bool native_full_equal = equal_full_native_output(
            client, native_server, native_server_context);
        if (!native_full_equal)
            std::fprintf(stderr, "  scenario: %s command %u\n",
                         spec.name, i);
        require(native_full_equal,
                "ABI client/native server full outputs diverged");

        require(equal_state(client.state, bridge_repeat.state),
                "independent ABI bridge state fields diverged");
        require(client.state_hash != 0 &&
                    client.state_hash == bridge_repeat.state_hash,
                "independent ABI bridge state hash diverged");
        require(client.collision_hash != 0 &&
                    client.collision_hash == bridge_repeat.collision_hash,
                "independent ABI bridge collision transcript diverged");
        require(equal_state(client.state, native_server_state),
                "ABI client/native server state fields diverged");
        require(native_server_state_hash != 0 &&
                    client.state_hash == native_server_state_hash,
                "ABI client/native server state hash diverged");
        const uint64_t client_callback_count =
            client_fixture.callback_count - client_callbacks_before;
        const uint64_t bridge_callback_count =
            bridge_repeat_fixture.callback_count - bridge_callbacks_before;
        const uint64_t native_callback_count =
            native_server_fixture.callback_count - native_callbacks_before;
        require(client_callback_count >= client.collision_query_count,
                "client collision query accounting exceeded callbacks");
        require(bridge_callback_count >=
                    bridge_repeat.collision_query_count,
                "repeat bridge query accounting exceeded callbacks");
        require(client_callback_count == bridge_callback_count,
                "independent ABI bridge callback count diverged");
        require(client_callback_count == native_callback_count,
                "ABI client/native server callback count diverged");
        require(client.collision_query_count ==
                    bridge_repeat.collision_query_count,
                "independent ABI bridge collision query count diverged");

        result.state_transcript_hash =
            append_u64(result.state_transcript_hash, client.state_hash);
        result.collision_transcript_hash = append_u64(
            result.collision_transcript_hash, client.collision_hash);
        result.replay_chain_hash = Worr_PredictionReplayChainHashV1(
            result.replay_chain_hash, i, client_command_hash,
            client.collision_hash, client.state_hash);
        result.collision_queries += client.collision_query_count;
        result.maximum_z = std::max(result.maximum_z, client.state.origin[2]);
        result.native_server_state_hash = native_server_state_hash;
    }

    result.state_hash = client.state_hash;
    require(result.native_server_state_hash == result.state_hash,
            "final ABI client/native server hash diverged");
    result.final_x = client.state.origin[0];
    result.final_z = client.state.origin[2];
    result.final_flags = client.state.movement_flags;
    require(result.collision_queries > 0,
            "scenario did not exercise the collision ABI");
    return result;
}

void validate_scenario_behaviour(
    const std::array<scenario_result_t, 5> &results)
{
    const scenario_result_t &empty = results[0];
    const scenario_result_t &floor = results[1];
    const scenario_result_t &wall = results[2];
    const scenario_result_t &step = results[3];
    const scenario_result_t &slope = results[4];

    require(empty.final_z < empty.initial_z,
            "empty fixture did not exercise falling movement");
    require((floor.final_flags & kPmfOnGround) != 0,
            "floor fixture did not finish grounded");
    require(std::fabs(floor.final_z - 24.0f) < 0.01f,
            "floor fixture did not preserve standing height");
    require(wall.final_x <= 56.01f && wall.final_x > 40.0f,
            "wall fixture did not stop at the expanded wall plane");
    require(step.maximum_z > step.initial_z + 8.0f,
            "step fixture did not step onto the analytic box");
    require(slope.final_x > slope.initial_x &&
                slope.final_z > slope.initial_z,
            "slope fixture did not move uphill");
}

std::array<coverage_spec_t, kCoverageCaseCount> make_coverage_specs()
{
    std::array<coverage_spec_t, kCoverageCaseCount> specs{};
    size_t count = 0;
    auto add = [&](const char *name, const char *category,
                   collision_fixture_t fixture,
                   worr_prediction_state_v1 state,
                   worr_prediction_config_v1 config,
                   worr_prediction_command_v1 command,
                   uint16_t required_flags = 0,
                   uint8_t minimum_water_level = 0,
                   bool require_upward_velocity = false) {
        require(count < specs.size(), "prediction coverage matrix overflow");
        specs[count++] = {name,
                          category,
                          fixture,
                          state,
                          config,
                          command,
                          required_flags,
                          minimum_water_level,
                          require_upward_velocity};
    };
    auto state_for_type = [](pmtype_t type, float z) {
        worr_prediction_state_v1 state = make_state(0.0f, 0.0f, z);
        state.movement_type = static_cast<int32_t>(type);
        return state;
    };
    auto command = [](uint8_t duration, uint8_t buttons = 0,
                      float forward = 180.0f) {
        worr_prediction_command_v1 value =
            make_command(0, forward, 0.0f);
        value.duration_ms = duration;
        value.buttons = buttons;
        return value;
    };
    auto timer_state = [](pmflags_t flag, bool airborne) {
        worr_prediction_state_v1 state =
            make_state(0.0f, 0.0f, airborne ? 80.0f : 24.0f);
        state.movement_flags = static_cast<uint16_t>(flag);
        state.movement_time_ms = 200;
        if (flag == PMF_TIME_WATERJUMP)
            state.velocity[2] = 200.0f;
        return state;
    };

    const worr_prediction_config_v1 base_config = make_config();
    add("pm-normal", "pm-type", floor_fixture(),
        state_for_type(PM_NORMAL, 24.0f), base_config, command(16));
    add("pm-grapple", "pm-type", empty_fixture(),
        state_for_type(PM_GRAPPLE, 80.0f), base_config, command(16));
    add("pm-noclip", "pm-type", empty_fixture(),
        state_for_type(PM_NOCLIP, 80.0f), base_config, command(16));
    add("pm-spectator", "pm-type", wall_fixture(),
        state_for_type(PM_SPECTATOR, 24.0f), base_config, command(16));
    add("pm-dead", "pm-type", floor_fixture(),
        state_for_type(PM_DEAD, 24.0f), base_config, command(16));
    add("pm-gib", "pm-type", floor_fixture(),
        state_for_type(PM_GIB, 0.0f), base_config, command(16));
    add("pm-freeze", "pm-type", empty_fixture(),
        state_for_type(PM_FREEZE, 80.0f), base_config, command(16));

    add("control-jump", "control", floor_fixture(),
        state_for_type(PM_NORMAL, 24.0f), base_config,
        command(16, static_cast<uint8_t>(BUTTON_JUMP)),
        static_cast<uint16_t>(PMF_JUMP_HELD), 0, true);
    add("control-duck", "control", floor_fixture(),
        state_for_type(PM_NORMAL, 24.0f), base_config,
        command(16, static_cast<uint8_t>(BUTTON_CROUCH)),
        static_cast<uint16_t>(PMF_DUCKED));

    add("timer-waterjump", "timer", empty_fixture(),
        timer_state(PMF_TIME_WATERJUMP, true), base_config, command(7),
        static_cast<uint16_t>(PMF_TIME_WATERJUMP), 0, true);
    add("timer-land", "timer", floor_fixture(),
        timer_state(PMF_TIME_LAND, false), base_config, command(7),
        static_cast<uint16_t>(PMF_TIME_LAND));
    add("timer-knockback", "timer", floor_fixture(),
        timer_state(PMF_TIME_KNOCKBACK, false), base_config, command(7),
        static_cast<uint16_t>(PMF_TIME_KNOCKBACK));
    add("timer-trick", "timer", floor_fixture(),
        timer_state(PMF_TIME_TRICK, false), base_config, command(7),
        static_cast<uint16_t>(PMF_TIME_TRICK));
    add("timer-spawn-lock", "timer", floor_fixture(),
        timer_state(PMF_TIME_SPAWN_LOCK, false), base_config, command(7),
        static_cast<uint16_t>(PMF_TIME_SPAWN_LOCK));

    worr_prediction_config_v1 n64_config = base_config;
    n64_config.flags |= WORR_PREDICTION_CONFIG_N64_PHYSICS;
    add("config-n64", "config", floor_fixture(),
        state_for_type(PM_NORMAL, 24.0f), n64_config, command(16));
    worr_prediction_state_v1 q3_collision_state =
        state_for_type(PM_NORMAL, 80.0f);
    q3_collision_state.origin[0] = 55.0f;
    q3_collision_state.velocity[0] = 200.0f;
    add("config-q3-control-default", "config", diagonal_wall_fixture(),
        q3_collision_state, base_config, command(7, 0, 0.0f));
    worr_prediction_config_v1 q3_config = base_config;
    q3_config.flags |= WORR_PREDICTION_CONFIG_Q3_OVERBOUNCE;
    add("config-q3-overbounce", "config", diagonal_wall_fixture(),
        q3_collision_state, q3_config, command(7, 0, 0.0f));
    worr_prediction_config_v1 zero_air_config = base_config;
    zero_air_config.air_acceleration = 0;
    add("config-air-accel-zero", "config", empty_fixture(),
        state_for_type(PM_NORMAL, 80.0f), zero_air_config, command(16));
    worr_prediction_config_v1 high_air_config = base_config;
    high_air_config.air_acceleration = 10;
    add("config-air-accel-high", "config", empty_fixture(),
        state_for_type(PM_NORMAL, 80.0f), high_air_config, command(16));

    for (const uint8_t duration :
         std::array<uint8_t, 6>{1, 7, 8, 9, 66, 250}) {
        static constexpr const char *duration_names[] = {
            "duration-1", "duration-7", "duration-8",
            "duration-9", "duration-66", "duration-250"};
        const size_t duration_index =
            duration == 1   ? 0
            : duration == 7 ? 1
            : duration == 8 ? 2
            : duration == 9 ? 3
            : duration == 66 ? 4
                             : 5;
        add(duration_names[duration_index], "duration", empty_fixture(),
            state_for_type(PM_NORMAL, 96.0f), base_config,
            command(duration));
    }

    add("contents-water-waist", "contents", water_fixture(),
        state_for_type(PM_NORMAL, 24.0f), base_config, command(16), 0, 2);
    add("contents-ladder", "contents", ladder_fixture(),
        state_for_type(PM_NORMAL, 24.0f), base_config, command(16),
        static_cast<uint16_t>(PMF_ON_LADDER));

    require(count == specs.size(), "prediction coverage matrix is incomplete");
    return specs;
}

coverage_result_t run_coverage_case(const coverage_spec_t &spec)
{
    collision_fixture_t client_fixture = spec.fixture;
    collision_fixture_t repeat_fixture = spec.fixture;
    collision_fixture_t native_fixture = spec.fixture;
    native_collision_context_t native_context{};
    native_context.fixture = &native_fixture;

    worr_prediction_command_v1 command = spec.command;
    require(Worr_PredictionCanonicalizeCommandV1(&command),
            "coverage command canonicalization failed");
    worr_prediction_step_v1 client =
        make_step(spec.initial_state, spec.config, &client_fixture);
    worr_prediction_step_v1 repeat =
        make_step(spec.initial_state, spec.config, &repeat_fixture);
    PMove native = make_native_move(spec.initial_state, spec.config);
    client.command = command;
    repeat.command = command;
    configure_native_step_inputs(native, client);

    require(Worr_PredictionStepV1(&client),
            "coverage ABI client step rejected");
    require(Worr_PredictionStepV1(&repeat),
            "coverage ABI repeat step rejected");
    run_native_move(native, native_context, command);
    const worr_prediction_state_v1 native_state =
        abi_state_from_native(native.s);
    const uint64_t native_hash = Worr_PredictionHashStateV1(&native_state);

    require(equal_full_output_values(client, repeat, true),
            "coverage independent ABI full outputs diverged");
    const bool native_full_equal =
        equal_full_native_output(client, native, native_context);
    if (!native_full_equal)
        std::fprintf(stderr, "  coverage case: %s\n", spec.name);
    require(native_full_equal,
            "coverage ABI/native full outputs diverged");

    require(equal_state(client.state, repeat.state) &&
                client.state_hash == repeat.state_hash &&
                client.collision_hash == repeat.collision_hash,
            "coverage independent ABI bridges diverged");
    require(equal_state(client.state, native_state) &&
                client.state_hash == native_hash,
            "coverage ABI client/native server state diverged");
    require(client.water_level == static_cast<uint8_t>(native.waterLevel) &&
                client.water_type == static_cast<uint32_t>(native.waterType) &&
                client.ground_entity_id ==
                    native_entity_id(native.groundEntity),
            "coverage ABI client/native server environment output diverged");
    require((client.state.movement_flags & spec.required_movement_flags) ==
                spec.required_movement_flags,
            "coverage case did not produce required movement flags");
    require(client.water_level >= spec.minimum_water_level,
            "coverage case did not reach its required water level");
    require(!spec.require_upward_velocity || client.state.velocity[2] > 0.0f,
            "coverage case did not retain required upward velocity");

    const uint64_t command_hash = Worr_PredictionHashCommandV1(&command);
    const uint64_t config_hash = Worr_PredictionHashConfigV1(&spec.config);
    require(command_hash != 0 && config_hash != 0,
            "coverage command/config hash rejected");
    return {spec.name,
            spec.category,
            command_hash,
            config_hash,
            client.state_hash,
            native_hash,
            client.collision_hash,
            client.collision_query_count,
            true};
}

void validate_coverage_relationships(
    const std::array<coverage_result_t, kCoverageCaseCount> &coverage)
{
    const coverage_result_t *default_control = nullptr;
    const coverage_result_t *q3_overbounce = nullptr;
    for (const coverage_result_t &item : coverage) {
        if (std::string_view(item.name) == "config-q3-control-default")
            default_control = &item;
        else if (std::string_view(item.name) == "config-q3-overbounce")
            q3_overbounce = &item;
    }
    require(default_control && q3_overbounce,
            "Q3 overbounce differential controls are missing");
    require(default_control->command_hash == q3_overbounce->command_hash,
            "Q3 overbounce control commands are not equivalent");
    require(default_control->config_hash != q3_overbounce->config_hash,
            "Q3 overbounce control configs did not differ");
    require(default_control->state_hash != q3_overbounce->state_hash &&
                default_control->native_server_state_hash !=
                    q3_overbounce->native_server_state_hash,
            "Q3 overbounce branch did not change wall-collision physics");
    require(default_control->collision_queries > 0 &&
                q3_overbounce->collision_queries > 0,
            "Q3 overbounce differential did not collide with the wall");
}

void record_behaviour(focused_fixture_result_t &result, const char *name,
                      bool condition, const char *failure_message)
{
    require(condition, failure_message);
    require(result.behaviour_assertion_count <
                result.behaviour_assertions.size(),
            "focused fixture behaviour assertion capacity exceeded");
    result.behaviour_assertions[result.behaviour_assertion_count++] = name;
}

worr_prediction_step_v1 persistent_step_snapshot(
    const worr_prediction_step_v1 &step)
{
    worr_prediction_step_v1 snapshot = step;
    snapshot.collision_context = nullptr;
    snapshot.trace = nullptr;
    snapshot.point_contents = nullptr;
    return snapshot;
}

void run_single_step_parity(
    worr_prediction_step_v1 &client, collision_fixture_t &client_fixture,
    collision_fixture_t &repeat_fixture,
    collision_fixture_t &native_fixture,
    worr_prediction_step_v1 &native_snapshot)
{
    client.collision_context = &client_fixture;
    worr_prediction_step_v1 repeat = client;
    repeat.collision_context = &repeat_fixture;

    native_collision_context_t native_context{};
    native_context.fixture = &native_fixture;
    PMove native = make_native_move(client.state, client.config);
    configure_native_step_inputs(native, client);

    require(Worr_PredictionStepV1(&client),
            "focused ABI prediction step rejected");
    require(Worr_PredictionStepV1(&repeat),
            "focused repeat ABI prediction step rejected");
    run_native_move(native, native_context, client.command);
    require(equal_full_output_values(client, repeat, true),
            "focused repeat ABI full output diverged");
    require(equal_full_native_output(client, native, native_context,
                                     &native_snapshot),
            "focused ABI/native full output diverged");
}

void capture_primary(focused_fixture_result_t &result,
                     const worr_prediction_step_v1 &input,
                     const worr_prediction_step_v1 &output,
                     const worr_prediction_step_v1 &native_output)
{
    result.initial_state = input.state;
    result.canonical_command = output.command;
    result.config = input.config;
    result.snap_initial = input.snap_initial;
    result.player_entity_id = input.player_entity_id;
    result.output = persistent_step_snapshot(output);
    result.native_output = persistent_step_snapshot(native_output);
    result.full_output_equal = true;
}

void capture_control(focused_fixture_result_t &result,
                     const worr_prediction_step_v1 &input,
                     const worr_prediction_step_v1 &output,
                     const worr_prediction_step_v1 &native_output)
{
    result.has_control = true;
    result.control_initial_state = input.state;
    result.control_command = output.command;
    result.control_config = input.config;
    result.control_snap_initial = input.snap_initial;
    result.control_output = persistent_step_snapshot(output);
    result.native_control_output = persistent_step_snapshot(native_output);
    result.control_full_output_equal = true;
}

bool has_touch_id(const worr_prediction_step_v1 &step, uint32_t entity_id)
{
    for (uint32_t i = 0; i < step.touch_count; ++i) {
        if (step.touch_entity_ids[i] == entity_id)
            return true;
    }
    return false;
}

focused_fixture_result_t run_angled_full_output_fixture()
{
    focused_fixture_result_t result{};
    result.name = "angled-full-output-floor";
    result.category = "full-output";

    collision_fixture_t client_fixture = floor_fixture();
    client_fixture.planes[0].surface_id = 17;
    client_fixture.planes[0].entity_id = 17;
    collision_fixture_t repeat_fixture = client_fixture;
    collision_fixture_t native_fixture = client_fixture;

    worr_prediction_state_v1 state = make_state(0.0f, 0.0f, 24.0f);
    state.delta_angles[0] = 11.25f;
    state.delta_angles[1] = -22.5f;
    state.delta_angles[2] = 3.75f;
    worr_prediction_step_v1 step =
        make_step(state, make_config(), &client_fixture);
    step.view_offset[0] = 1.0f;
    step.view_offset[1] = -2.0f;
    step.view_offset[2] = 3.0f;
    step.command.duration_ms = 17;
    step.command.view_angles[0] = 37.1234f;
    step.command.view_angles[1] = 123.456f;
    step.command.view_angles[2] = -18.75f;
    step.command.forward_move = 220.75f;
    step.command.side_move = -37.5f;
    require(Worr_PredictionCanonicalizeCommandV1(&step.command),
            "angled command canonicalization failed");
    const worr_prediction_step_v1 input = step;
    worr_prediction_step_v1 native_output{};
    run_single_step_parity(step, client_fixture, repeat_fixture,
                           native_fixture, native_output);
    capture_primary(result, input, step, native_output);

    const float expected_mins[3] = {-16.0f, -16.0f, -24.0f};
    const float expected_maxs[3] = {16.0f, 16.0f, 32.0f};
    record_behaviour(result, "nonzero_canonical_angles",
                     step.command.view_angles[0] != 0.0f &&
                         step.command.view_angles[1] != 0.0f &&
                         step.command.view_angles[2] != 0.0f,
                     "angled fixture did not retain nonzero canonical angles");
    record_behaviour(result, "canonical_short_angles_exact",
                     equal_float_bits(step.command.view_angles[0],
                                      37.122802734375f) &&
                         equal_float_bits(step.command.view_angles[1],
                                          123.453369140625f) &&
                         equal_float_bits(step.command.view_angles[2],
                                          -18.7481689453125f),
                     "angled fixture short-angle representation changed");
    record_behaviour(result, "movement_truncated_to_wire_units",
                     equal_float_bits(step.command.forward_move, 220.0f) &&
                         equal_float_bits(step.command.side_move, -37.0f),
                     "angled fixture movement was not wire-canonical");
    record_behaviour(result, "view_angles_include_delta",
                     equal_float_bits(
                         step.view_angles[0],
                         step.command.view_angles[0] +
                             state.delta_angles[0]) &&
                         equal_float_bits(
                             step.view_angles[1],
                             step.command.view_angles[1] +
                                 state.delta_angles[1]) &&
                         equal_float_bits(
                             step.view_angles[2],
                             step.command.view_angles[2] +
                                 state.delta_angles[2]),
                     "angled fixture view/delta result changed");
    record_behaviour(result, "standing_bounds_exact",
                     equal_vec(step.mins, expected_mins) &&
                         equal_vec(step.maxs, expected_maxs),
                     "angled fixture standing bounds changed");
    record_behaviour(result, "floor_plane_exact",
                     equal_float_bits(step.ground_plane.normal[0], 0.0f) &&
                         equal_float_bits(step.ground_plane.normal[1], 0.0f) &&
                         equal_float_bits(step.ground_plane.normal[2], 1.0f) &&
                         equal_float_bits(step.ground_plane.distance, 0.0f) &&
                         step.ground_plane.type == 2 &&
                         step.ground_plane.sign_bits == 0 &&
                         step.ground_entity_id == 17,
                     "angled fixture floor identity/plane changed");
    record_behaviour(result, "full_native_output_equal",
                     result.full_output_equal,
                     "angled fixture full native parity missing");
    record_behaviour(result, "nonzero_delta_angles",
                     state.delta_angles[0] != 0.0f &&
                         state.delta_angles[1] != 0.0f &&
                         state.delta_angles[2] != 0.0f,
                     "angled fixture did not exercise nonzero delta angles");
    record_behaviour(result, "all_step_output_fields",
                     result.full_output_equal,
                     "angled fixture did not compare every step output field");
    record_behaviour(result, "bitwise_native_parity",
                     result.full_output_equal,
                     "angled fixture bitwise native parity missing");
    return result;
}

focused_fixture_result_t run_snap_initial_fixture()
{
    focused_fixture_result_t result{};
    result.name = "snap-initial-stuck-order";
    result.category = "snap-initial";

    collision_fixture_t client_fixture = floor_fixture();
    client_fixture.planes[0].surface_id = 17;
    client_fixture.planes[0].entity_id = 17;
    collision_fixture_t repeat_fixture = client_fixture;
    collision_fixture_t native_fixture = client_fixture;
    worr_prediction_state_v1 state = make_state(0.0f, 0.0f, 23.5f);
    worr_prediction_step_v1 step =
        make_step(state, make_config(), &client_fixture);
    step.snap_initial = 1;
    step.command.duration_ms = 0;
    step.command.forward_move = 0.0f;
    step.command.side_move = 0.0f;
    require(Worr_PredictionCanonicalizeCommandV1(&step.command),
            "snap command canonicalization failed");
    const worr_prediction_step_v1 input = step;
    worr_prediction_step_v1 native_output{};
    run_single_step_parity(step, client_fixture, repeat_fixture,
                           native_fixture, native_output);
    capture_primary(result, input, step, native_output);

    collision_fixture_t control_client = floor_fixture();
    control_client.planes[0].surface_id = 17;
    control_client.planes[0].entity_id = 17;
    collision_fixture_t control_repeat = control_client;
    collision_fixture_t control_native = control_client;
    worr_prediction_step_v1 control =
        make_step(state, make_config(), &control_client);
    control.snap_initial = 0;
    control.command = input.command;
    const worr_prediction_step_v1 control_input = control;
    worr_prediction_step_v1 native_control{};
    run_single_step_parity(control, control_client, control_repeat,
                           control_native, native_control);
    capture_control(result, control_input, control, native_control);

    record_behaviour(result, "penetration_observed",
                     client_fixture.saw_start_solid &&
                         client_fixture.saw_all_solid,
                     "snap fixture did not exercise penetrated/all-solid input");
    record_behaviour(result, "snap_selected_positive_z_candidate",
                     equal_float_bits(step.state.origin[0], 0.0f) &&
                         equal_float_bits(step.state.origin[1], 0.0f) &&
                         equal_float_bits(step.state.origin[2], 24.5f),
                     "initial snap total-order candidate changed");
    record_behaviour(result, "snap_control_differs",
                     !equal_state(step.state, control.state),
                     "snap_initial did not differ from penetrated control");
    record_behaviour(result, "stuck_control_total_order_exact",
                     equal_float_bits(control.state.origin[0], 0.125f) &&
                         equal_float_bits(control.state.origin[1], 0.0f) &&
                         equal_float_bits(control.state.origin[2], 24.5f),
                     "penetrated control total-order result changed");
    record_behaviour(result, "snap_full_native_output_equal",
                     result.full_output_equal &&
                         result.control_full_output_equal,
                     "snap fixture full native parity missing");
    record_behaviour(result, "startsolid",
                     client_fixture.saw_start_solid,
                     "snap fixture did not observe startsolid");
    record_behaviour(result, "allsolid",
                     client_fixture.saw_all_solid,
                     "snap fixture did not observe allsolid");
    record_behaviour(result, "snap_initial_success",
                     equal_float_bits(step.state.origin[2], 24.5f),
                     "snap_initial did not select a valid candidate");
    record_behaviour(result, "snap_disabled_control",
                     control.snap_initial == 0 &&
                         !equal_state(step.state, control.state),
                     "snap-disabled control did not remain distinct");
    record_behaviour(result, "exact_total_order_result",
                     equal_float_bits(step.state.origin[0], 0.0f) &&
                         equal_float_bits(step.state.origin[1], 0.0f) &&
                         equal_float_bits(step.state.origin[2], 24.5f) &&
                         equal_float_bits(control.state.origin[0], 0.125f) &&
                         equal_float_bits(control.state.origin[1], 0.0f) &&
                         equal_float_bits(control.state.origin[2], 24.5f),
                     "snap/stuck total-order outputs changed");
    return result;
}

collision_fixture_t entity_player_fixture()
{
    collision_fixture_t fixture = floor_fixture();
    fixture.planes[0].surface_id = 17;
    fixture.planes[0].entity_id = 17;
    fixture.boxes[0] = {{{40.0f, -64.0f, 0.0f}},
                        {{56.0f, 64.0f, 96.0f}},
                        42,
                        kContentsPlayer,
                        5,
                        42};
    fixture.box_count = 1;
    fixture.expected_player_id = 7;
    fixture.dual_surface_primary_id = 42;
    fixture.dual_surface_normal = {{-0.707106781f, 0.707106781f, 0.0f}};
    fixture.dual_surface_distance = -16.9705627f;
    fixture.dual_surface_id = 43;
    fixture.dual_surface_flags = 9;
    return fixture;
}

focused_fixture_result_t run_entity_touch_surface_fixture()
{
    focused_fixture_result_t result{};
    result.name = "entity-touch-dual-surface-mask";
    result.category = "collision-identity";

    collision_fixture_t client_fixture = entity_player_fixture();
    collision_fixture_t repeat_fixture = client_fixture;
    collision_fixture_t native_fixture = client_fixture;
    worr_prediction_state_v1 state = make_state(0.0f, 0.0f, 24.0f);
    worr_prediction_step_v1 step =
        make_step(state, make_config(), &client_fixture);
    step.command.duration_ms = 250;
    step.command.forward_move = 400.0f;
    require(Worr_PredictionCanonicalizeCommandV1(&step.command),
            "entity-touch command canonicalization failed");
    const worr_prediction_step_v1 input = step;
    worr_prediction_step_v1 native_output{};
    run_single_step_parity(step, client_fixture, repeat_fixture,
                           native_fixture, native_output);
    capture_primary(result, input, step, native_output);

    collision_fixture_t ignore_client = entity_player_fixture();
    collision_fixture_t ignore_repeat = ignore_client;
    collision_fixture_t ignore_native = ignore_client;
    worr_prediction_state_v1 ignore_state = state;
    ignore_state.movement_flags |= kPmfIgnorePlayerCollision;
    worr_prediction_step_v1 ignore =
        make_step(ignore_state, make_config(), &ignore_client);
    ignore.command = input.command;
    const worr_prediction_step_v1 ignore_input = ignore;
    worr_prediction_step_v1 native_ignore{};
    run_single_step_parity(ignore, ignore_client, ignore_repeat,
                           ignore_native, native_ignore);
    capture_control(result, ignore_input, ignore, native_ignore);

    collision_fixture_t spectator_client = entity_player_fixture();
    collision_fixture_t spectator_repeat = spectator_client;
    collision_fixture_t spectator_native = spectator_client;
    worr_prediction_state_v1 spectator_state = state;
    spectator_state.movement_type = kPmSpectator;
    worr_prediction_step_v1 spectator =
        make_step(spectator_state, make_config(), &spectator_client);
    spectator.command.duration_ms = 16;
    spectator.command.forward_move = 180.0f;
    require(Worr_PredictionCanonicalizeCommandV1(&spectator.command),
            "spectator world-only command canonicalization failed");
    worr_prediction_step_v1 native_spectator{};
    run_single_step_parity(spectator, spectator_client,
                           spectator_repeat, spectator_native,
                           native_spectator);

    record_behaviour(result, "stable_nonworld_ground_entity",
                     step.ground_entity_id == 17 &&
                         ignore.ground_entity_id == 17,
                     "non-world floor identity did not survive the bridge");
    record_behaviour(result, "ordered_touch_contains_player",
                     step.touch_count == 2 &&
                         step.touch_entity_ids[0] == 17 &&
                         step.touch_entity_ids[1] == 42,
                     "player-solid touch identity was not reported");
    record_behaviour(result, "dual_surface_consumed",
                     client_fixture.dual_surface_return_count > 0,
                     "dual-surface collision result was not exercised");
    record_behaviour(result, "pass_and_world_query_identity",
                     client_fixture.saw_player_pass &&
                         spectator_client.saw_world_only,
                     "trace pass/world-only identity was not preserved");
    record_behaviour(result, "ignore_player_mask_differential",
                     client_fixture.saw_player_contents_mask &&
                         ignore_client.saw_mask_without_player &&
                         step.state_hash != ignore.state_hash &&
                         step.state.origin[0] < ignore.state.origin[0],
                     "ignore-player mask did not change controlled collision");
    record_behaviour(result, "collision_identity_positions_exact",
                     equal_float_bits(step.state.origin[0], 24.0f) &&
                         equal_float_bits(ignore.state.origin[0], 75.0f),
                     "entity collision/ignore-player positions changed");
    record_behaviour(result, "stable_entity_surface_token_set",
                     step.ground_entity_id == 17 &&
                         has_touch_id(step, 42) &&
                         client_fixture.dual_surface_return_count > 0,
                     "stable entity/surface token set was not retained");
    record_behaviour(result, "identity_full_native_output_equal",
                     result.full_output_equal &&
                         result.control_full_output_equal,
                     "entity/touch fixture full native parity missing");
    record_behaviour(result, "nonworld_ground_entity",
                     step.ground_entity_id == 17,
                     "focused collision fixture lacked non-world ground ID");
    record_behaviour(result, "ordered_touch_identity",
                     step.touch_count == 2 &&
                         step.touch_entity_ids[0] == 17 &&
                         step.touch_entity_ids[1] == 42,
                     "focused collision touch order changed");
    record_behaviour(result, "dual_surface",
                     client_fixture.dual_surface_return_count > 0,
                     "focused collision did not return a second surface");
    record_behaviour(result, "pass_entity",
                     client_fixture.saw_player_pass,
                     "focused collision did not preserve pass entity");
    record_behaviour(result, "world_only_query",
                     spectator_client.saw_world_only,
                     "focused spectator did not issue world-only query");
    record_behaviour(result, "ignore_player_mask_control",
                     ignore_client.saw_mask_without_player &&
                         step.state.origin[0] < ignore.state.origin[0],
                     "focused ignore-player control did not differ");
    return result;
}

void run_movement_stream(
    worr_prediction_step_v1 &client,
    collision_fixture_t &client_fixture,
    collision_fixture_t &repeat_fixture,
    collision_fixture_t &native_fixture,
    uint32_t command_count,
    worr_prediction_step_v1 &native_snapshot)
{
    client.collision_context = &client_fixture;
    worr_prediction_step_v1 repeat = client;
    repeat.collision_context = &repeat_fixture;
    native_collision_context_t native_context{};
    native_context.fixture = &native_fixture;
    PMove native = make_native_move(client.state, client.config);
    configure_native_step_inputs(native, client);

    for (uint32_t i = 0; i < command_count; ++i) {
        worr_prediction_command_v1 command = make_command(i, 400.0f, 0.0f);
        command.duration_ms = 16;
        command.forward_move = 400.0f;
        require(Worr_PredictionCanonicalizeCommandV1(&command),
                "movement-stream command canonicalization failed");
        client.command = command;
        repeat.command = command;
        require(Worr_PredictionStepV1(&client),
                "movement-stream ABI step rejected");
        require(Worr_PredictionStepV1(&repeat),
                "movement-stream repeat ABI step rejected");
        run_native_move(native, native_context, command);
        require(equal_full_output_values(client, repeat, true),
                "movement-stream repeat ABI full output diverged");
        require(equal_full_native_output(client, native, native_context,
                                         &native_snapshot),
                "movement-stream ABI/native full output diverged");
    }
}

focused_fixture_result_t run_haste_fixture()
{
    focused_fixture_result_t result{};
    result.name = "haste-speed-differential";
    result.category = "movement-flag";

    collision_fixture_t haste_client = floor_fixture();
    collision_fixture_t haste_repeat = haste_client;
    collision_fixture_t haste_native = haste_client;
    worr_prediction_state_v1 haste_state =
        make_state(0.0f, 0.0f, 24.0f);
    haste_state.movement_flags |= kPmfHaste;
    worr_prediction_step_v1 haste =
        make_step(haste_state, make_config(), &haste_client);
    const worr_prediction_step_v1 haste_input = haste;
    worr_prediction_step_v1 native_haste{};
    run_movement_stream(haste, haste_client, haste_repeat, haste_native,
                        48, native_haste);
    capture_primary(result, haste_input, haste, native_haste);

    collision_fixture_t control_client = floor_fixture();
    collision_fixture_t control_repeat = control_client;
    collision_fixture_t control_native = control_client;
    worr_prediction_state_v1 control_state =
        make_state(0.0f, 0.0f, 24.0f);
    worr_prediction_step_v1 control =
        make_step(control_state, make_config(), &control_client);
    const worr_prediction_step_v1 control_input = control;
    worr_prediction_step_v1 native_control{};
    run_movement_stream(control, control_client, control_repeat,
                        control_native, 48, native_control);
    capture_control(result, control_input, control, native_control);

    record_behaviour(result, "haste_flag_persists",
                     (haste.state.movement_flags & kPmfHaste) != 0,
                     "haste flag did not persist through movement");
    record_behaviour(result, "default_speed_cap_exact",
                     equal_float_bits(control.state.velocity[0], 300.0f) &&
                         equal_float_bits(control.state.velocity[1], 0.0f),
                     "default movement speed cap changed");
    record_behaviour(result, "haste_speed_cap_exact",
                     equal_float_bits(haste.state.velocity[0], 375.0f) &&
                         equal_float_bits(haste.state.velocity[1], 0.0f),
                     "haste movement speed cap changed");
    record_behaviour(result, "haste_changes_state",
                     haste.state_hash != control.state_hash &&
                         haste.state.origin[0] > control.state.origin[0],
                     "haste did not produce a controlled state differential");
    record_behaviour(result, "haste_full_native_output_equal",
                     result.full_output_equal &&
                         result.control_full_output_equal,
                     "haste fixture full native parity missing");
    record_behaviour(result, "default_speed_cap",
                     equal_float_bits(control.state.velocity[0], 300.0f),
                     "default speed-cap requirement changed");
    record_behaviour(result, "haste_speed_cap",
                     equal_float_bits(haste.state.velocity[0], 375.0f),
                     "haste speed-cap requirement changed");
    record_behaviour(result, "flag_persistence",
                     (haste.state.movement_flags & kPmfHaste) != 0,
                     "haste flag-persistence requirement changed");
    record_behaviour(result, "multi_command_full_output_parity",
                     result.full_output_equal &&
                         result.control_full_output_equal,
                     "haste multi-command full-output parity missing");
    return result;
}

focused_fixture_result_t run_grapple_fixture()
{
    focused_fixture_result_t result{};
    result.name = "grapple-velocity-wall";
    result.category = "movement-type";

    collision_fixture_t client_fixture = wall_fixture();
    client_fixture.planes[1].entity_id = 42;
    collision_fixture_t repeat_fixture = client_fixture;
    collision_fixture_t native_fixture = client_fixture;
    worr_prediction_state_v1 state = make_state(0.0f, 0.0f, 80.0f);
    state.movement_type = kPmGrapple;
    state.velocity[0] = 300.0f;
    state.velocity[2] = -100.0f;
    worr_prediction_step_v1 step =
        make_step(state, make_config(), &client_fixture);
    step.command.duration_ms = 250;
    step.command.forward_move = 0.0f;
    require(Worr_PredictionCanonicalizeCommandV1(&step.command),
            "grapple command canonicalization failed");
    const worr_prediction_step_v1 input = step;
    worr_prediction_step_v1 native_output{};
    run_single_step_parity(step, client_fixture, repeat_fixture,
                           native_fixture, native_output);
    capture_primary(result, input, step, native_output);

    record_behaviour(result, "grapple_retains_type",
                     step.state.movement_type == kPmGrapple,
                     "grapple movement type did not persist");
    record_behaviour(result, "grapple_collides_with_wall",
                     std::bit_cast<uint32_t>(step.state.origin[0]) ==
                             UINT32_C(0x425f3d71) &&
                         std::bit_cast<uint32_t>(step.state.origin[2]) ==
                             UINT32_C(0x425c0001) &&
                         has_touch_id(step, 42),
                     "grapple fixture did not collide with its wall");
    record_behaviour(result, "grapple_skips_gravity",
                     equal_float_bits(step.state.velocity[0], -3.0f) &&
                         equal_float_bits(step.state.velocity[2], -100.0f),
                     "grapple movement unexpectedly applied gravity");
    record_behaviour(result, "grapple_full_native_output_equal",
                     result.full_output_equal,
                     "grapple fixture full native parity missing");
    record_behaviour(result, "nonzero_initial_velocity",
                     state.velocity[0] != 0.0f &&
                         state.velocity[2] != 0.0f,
                     "grapple fixture initial velocity was zero");
    record_behaviour(result, "wall_collision",
                     has_touch_id(step, 42),
                     "grapple fixture wall collision requirement changed");
    record_behaviour(result, "no_gravity",
                     equal_float_bits(step.state.velocity[2], -100.0f),
                     "grapple fixture no-gravity requirement changed");
    record_behaviour(result, "ordered_touch_identity",
                     step.touch_count == 1 &&
                         step.touch_entity_ids[0] == 42,
                     "grapple touch identity/order changed");
    return result;
}

collision_fixture_t presentation_water_fixture(bool water)
{
    collision_fixture_t fixture = floor_fixture();
    if (water) {
        fixture.volumes[0] = {{{-128.0f, -128.0f, 49.0f}},
                              {{128.0f, 128.0f, 51.0f}},
                              kContentsWater};
        fixture.volume_count = 1;
    }
    return fixture;
}

focused_fixture_result_t run_presentation_exclusion_fixture()
{
    focused_fixture_result_t result{};
    result.name = "presentation-water-hash-exclusion";
    result.category = "presentation-exclusion";

    collision_fixture_t wet_client = presentation_water_fixture(true);
    collision_fixture_t wet_repeat = wet_client;
    collision_fixture_t wet_native = wet_client;
    worr_prediction_state_v1 state = make_state(0.0f, 0.0f, 24.0f);
    worr_prediction_step_v1 wet =
        make_step(state, make_config(), &wet_client);
    wet.command.duration_ms = 0;
    wet.command.forward_move = 0.0f;
    require(Worr_PredictionCanonicalizeCommandV1(&wet.command),
            "presentation command canonicalization failed");
    const worr_prediction_step_v1 wet_input = wet;
    worr_prediction_step_v1 native_wet{};
    run_single_step_parity(wet, wet_client, wet_repeat, wet_native,
                           native_wet);
    capture_primary(result, wet_input, wet, native_wet);

    collision_fixture_t dry_client = presentation_water_fixture(false);
    collision_fixture_t dry_repeat = dry_client;
    collision_fixture_t dry_native = dry_client;
    worr_prediction_step_v1 dry =
        make_step(state, make_config(), &dry_client);
    dry.command = wet_input.command;
    const worr_prediction_step_v1 dry_input = dry;
    worr_prediction_step_v1 native_dry{};
    run_single_step_parity(dry, dry_client, dry_repeat, dry_native,
                           native_dry);
    capture_control(result, dry_input, dry, native_dry);

    if ((wet.rd_flags & static_cast<uint8_t>(RDF_UNDERWATER)) == 0 ||
        dry.rd_flags != 0) {
        std::fprintf(stderr,
                     "  presentation flags: wet=%u dry=%u origin_z=%.9g "
                     "view_height=%d point_callbacks=%llu\n",
                     wet.rd_flags, dry.rd_flags,
                     static_cast<double>(wet.state.origin[2]),
                     static_cast<int>(wet.state.view_height),
                     static_cast<unsigned long long>(
                         wet_client.point_callback_count));
    }

    record_behaviour(result, "presentation_sets_underwater_flag",
                     (wet.rd_flags & static_cast<uint8_t>(RDF_UNDERWATER)) !=
                         0 && dry.rd_flags == 0,
                     "presentation-only water flag changed");
    record_behaviour(result, "presentation_water_blend_exact",
                     equal_float_bits(wet.screen_blend[0], 0.5f) &&
                         equal_float_bits(wet.screen_blend[1], 0.3f) &&
                         equal_float_bits(wet.screen_blend[2], 0.2f) &&
                         equal_float_bits(wet.screen_blend[3], 0.4f),
                     "presentation-only water blend changed");
    record_behaviour(result, "presentation_does_not_set_movement_water",
                     wet.water_level == WATER_NONE &&
                         wet.water_type == 0,
                     "presentation-only volume affected movement water state");
    record_behaviour(result, "presentation_excluded_from_hashes",
                     wet.state_hash == dry.state_hash &&
                         wet.collision_hash == dry.collision_hash &&
                         wet.collision_query_count ==
                             dry.collision_query_count,
                     "presentation-only contents changed movement hashes");
    record_behaviour(result, "one_presentation_query_per_control",
                     wet_client.callback_count -
                             wet.collision_query_count == 1 &&
                         dry_client.callback_count -
                             dry.collision_query_count == 1,
                     "presentation fixture callback accounting changed");
    record_behaviour(result, "presentation_full_native_output_equal",
                     result.full_output_equal &&
                         result.control_full_output_equal,
                     "presentation fixture full native parity missing");
    record_behaviour(result, "exact_water_blend",
                     equal_float_bits(wet.screen_blend[0], 0.5f) &&
                         equal_float_bits(wet.screen_blend[1], 0.3f) &&
                         equal_float_bits(wet.screen_blend[2], 0.2f) &&
                         equal_float_bits(wet.screen_blend[3], 0.4f),
                     "presentation exact-water-blend requirement changed");
    record_behaviour(result, "underwater_render_flag",
                     (wet.rd_flags &
                      static_cast<uint8_t>(RDF_UNDERWATER)) != 0,
                     "presentation underwater-render-flag requirement changed");
    record_behaviour(result, "unchanged_state_hash",
                     wet.state_hash == dry.state_hash,
                     "presentation-only water changed state hash");
    record_behaviour(result, "unchanged_collision_hash",
                     wet.collision_hash == dry.collision_hash,
                     "presentation-only water changed collision hash");
    record_behaviour(result, "dry_control",
                     dry.rd_flags == 0 &&
                         equal_float_bits(dry.screen_blend[0], 0.0f) &&
                         equal_float_bits(dry.screen_blend[1], 0.0f) &&
                         equal_float_bits(dry.screen_blend[2], 0.0f) &&
                         equal_float_bits(dry.screen_blend[3], 0.0f),
                     "presentation dry-control requirement changed");
    return result;
}

std::array<focused_fixture_result_t, kFocusedFixtureCount>
run_focused_fixtures()
{
    return {{run_angled_full_output_fixture(),
             run_snap_initial_fixture(),
             run_entity_touch_surface_fixture(),
             run_haste_fixture(),
             run_grapple_fixture(),
             run_presentation_exclusion_fixture()}};
}

fail_closed_result_t validate_fail_closed_cases()
{
    fail_closed_result_t result{};
    result.transcript_hash = begin_transcript();
    auto record = [&](const char *name, bool passed, const char *message) {
        require(passed, message);
        require(result.passed_cases < result.case_names.size(),
                "prediction fail-closed case-name capacity exceeded");
        result.case_names[result.passed_cases] = name;
        result.transcript_hash =
            append_string(result.transcript_hash, name);
        ++result.passed_cases;
    };
    auto atomically_rejected = [](worr_prediction_step_v1 step) {
        const worr_prediction_step_v1 before = step;
        return !Worr_PredictionStepV1(&step) &&
               std::memcmp(&step, &before, sizeof(step)) == 0;
    };

    record("null-step", !Worr_PredictionStepV1(nullptr),
           "prediction step accepted a null input");

    const worr_prediction_config_v1 config = make_config();
    collision_fixture_t fixture = floor_fixture();
    const worr_prediction_state_v1 state = make_state(0.0f, 0.0f, 24.0f);
    worr_prediction_step_v1 step = make_step(state, config, &fixture);
    step.command = make_command(0, 180.0f, 0.0f);

    worr_prediction_step_v1 invalid = step;
    invalid.state.origin[0] = std::numeric_limits<float>::quiet_NaN();
    record("non-finite-state", atomically_rejected(invalid),
           "prediction step did not atomically reject a non-finite state");

    invalid = step;
    invalid.state.movement_flags |= UINT16_C(1) << 14;
    record("unknown-movement-flag", atomically_rejected(invalid),
           "prediction step accepted an unknown movement flag");

    invalid = step;
    invalid.command.forward_move =
        std::numeric_limits<float>::infinity();
    record("non-finite-command", atomically_rejected(invalid),
           "prediction step did not atomically reject a non-finite command");

    invalid = step;
    invalid.command.reserved0 = 1;
    record("command-reserved-bits", atomically_rejected(invalid),
           "prediction step accepted non-zero command reserved bits");

    invalid = step;
    ++invalid.config.movement_model_revision;
    record("unknown-model-revision", atomically_rejected(invalid),
           "prediction step accepted an unknown movement model");

    invalid = step;
    invalid.config.flags = UINT32_C(0x80000000);
    record("unknown-config-flags", atomically_rejected(invalid),
           "prediction step accepted unknown config flags");

    invalid = step;
    invalid.snap_initial = 2;
    record("non-boolean-snap", atomically_rejected(invalid),
           "prediction step accepted a non-Boolean snap flag");

    invalid = step;
    invalid.trace = nullptr;
    record("null-trace-callback", atomically_rejected(invalid),
           "prediction step accepted a null trace callback");

    struct malformed_case_t {
        malformed_trace_mode_t mode;
        const char *name;
    };
    for (const malformed_case_t malformed_case : {
             malformed_case_t{
                 malformed_trace_mode_t::non_finite_fraction,
                 "trace-non-finite-fraction"},
             malformed_case_t{
                 malformed_trace_mode_t::surface_without_id,
                 "trace-surface-without-id"},
             malformed_case_t{
                 malformed_trace_mode_t::invalid_struct_size,
                 "trace-invalid-struct-size"},
             malformed_case_t{
                 malformed_trace_mode_t::conflicting_surface_flags,
                 "trace-conflicting-surface-flags"}}) {
        malformed_trace_context_t malformed{};
        malformed.fixture = floor_fixture();
        malformed.mode = malformed_case.mode;
        invalid = make_step(state, config, &malformed.fixture);
        invalid.command = step.command;
        invalid.collision_context = &malformed;
        invalid.trace = malformed_prediction_trace;
        invalid.point_contents = malformed_prediction_point_contents;
        record(malformed_case.name, atomically_rejected(invalid),
               "prediction step did not fail closed on malformed trace output");
    }

    invalid = step;
    invalid.trace = throwing_prediction_trace;
    record("trace-throws", atomically_rejected(invalid),
           "prediction step did not atomically reject a throwing callback");

    worr_prediction_command_v1 invalid_command = step.command;
    invalid_command.view_angles[0] =
        std::numeric_limits<float>::quiet_NaN();
    const worr_prediction_command_v1 command_before = invalid_command;
    record("canonicalize-nan",
           !Worr_PredictionCanonicalizeCommandV1(&invalid_command) &&
               std::memcmp(&invalid_command, &command_before,
                           sizeof(invalid_command)) == 0,
           "command canonicalization did not atomically reject NaN");

    uint32_t replay_count = UINT32_C(0xdeadbeef);
    record("replay-zero-capacity",
           !Worr_PredictionReplayCountV1(1, 2, 0, &replay_count) &&
               replay_count == UINT32_C(0xdeadbeef),
           "replay plan accepted zero capacity");
    record("replay-null-count",
           !Worr_PredictionReplayCountV1(1, 2, 16, nullptr),
           "replay plan accepted a null result pointer");
    replay_count = UINT32_C(0xdeadbeef);
    record("replay-range-fills-capacity",
           !Worr_PredictionReplayCountV1(UINT32_MAX - 1u, 6u, 8,
                                         &replay_count) &&
               replay_count == UINT32_C(0xdeadbeef),
           "replay plan accepted a wrapped range that fills capacity");
    uint32_t sequence = UINT32_C(0xdeadbeef);
    record("replay-index-out-of-range",
           !Worr_PredictionReplaySequenceV1(10, 4, 4, &sequence) &&
               sequence == UINT32_C(0xdeadbeef),
           "replay plan accepted an out-of-range index");
    record("replay-null-sequence",
           !Worr_PredictionReplaySequenceV1(10, 4, 0, nullptr),
           "replay plan accepted a null sequence result");

    require(result.passed_cases == kFailClosedCaseCount,
            "prediction fail-closed matrix case count changed");
    return result;
}

hash_contract_result_t validate_hash_contract()
{
    hash_contract_result_t result{};

    worr_prediction_state_v1 state = make_state(0.0f, 0.0f, 24.0f);
    worr_prediction_state_v1 equivalent_state = state;
    equivalent_state.origin[0] = -0.0f;
    result.state_hash = Worr_PredictionHashStateV1(&state);
    require(result.state_hash != 0 &&
                result.state_hash ==
                    Worr_PredictionHashStateV1(&equivalent_state),
            "state hash did not canonicalize zero or ignored fields");
    equivalent_state.origin[1] = 1.0f;
    require(result.state_hash !=
                Worr_PredictionHashStateV1(&equivalent_state),
            "state hash ignored a semantic field mutation");
    equivalent_state = state;
    equivalent_state.reserved0 = 1;
    require(Worr_PredictionHashStateV1(&equivalent_state) == 0,
            "state hash accepted a non-zero reserved field");

    worr_prediction_command_v1 command = make_command(3, 220.0f, 0.0f);
    worr_prediction_command_v1 equivalent_command = command;
    result.command_hash = Worr_PredictionHashCommandV1(&command);
    require(result.command_hash != 0 &&
                result.command_hash ==
                    Worr_PredictionHashCommandV1(&equivalent_command),
            "command hash included a reserved field");
    equivalent_command.forward_move += 1.0f;
    require(result.command_hash !=
                Worr_PredictionHashCommandV1(&equivalent_command),
            "command hash ignored a semantic field mutation");
    equivalent_command = command;
    equivalent_command.reserved0 = 1;
    require(Worr_PredictionHashCommandV1(&equivalent_command) == 0,
            "command hash accepted a non-zero reserved field");

    worr_prediction_command_v1 wire_command = command;
    wire_command.view_angles[1] = 360.0f;
    wire_command.forward_move = 220.75f;
    const uint64_t wire_hash = Worr_PredictionHashCommandV1(&wire_command);
    require(Worr_PredictionCanonicalizeCommandV1(&wire_command),
            "valid command canonicalization failed");
    require(wire_command.view_angles[1] == 0.0f &&
                wire_command.forward_move == 220.0f &&
                wire_hash == Worr_PredictionHashCommandV1(&wire_command),
            "command hash did not use the canonical wire representation");

    worr_prediction_config_v1 config = make_config();
    worr_prediction_config_v1 equivalent_config = config;
    result.config_hash = Worr_PredictionHashConfigV1(&config);
    require(result.config_hash != 0 &&
                result.config_hash ==
                    Worr_PredictionHashConfigV1(&equivalent_config),
            "equivalent config hashes diverged");
    equivalent_config.flags |= WORR_PREDICTION_CONFIG_Q3_OVERBOUNCE;
    require(result.config_hash !=
                Worr_PredictionHashConfigV1(&equivalent_config),
            "config hash ignored a semantic field mutation");
    equivalent_config.flags = UINT32_C(0x80000000);
    require(Worr_PredictionHashConfigV1(&equivalent_config) == 0,
            "config hash accepted an unknown flag");
    equivalent_config = config;
    equivalent_config.movement_model_revision++;
    require(Worr_PredictionHashConfigV1(&equivalent_config) == 0,
            "config hash accepted an unknown movement model revision");

    worr_prediction_state_v1 invalid_state = state;
    invalid_state.schema_version++;
    require(Worr_PredictionHashStateV1(&invalid_state) == 0,
            "state hash accepted an unknown schema");
    worr_prediction_command_v1 invalid_command = command;
    invalid_command.struct_size--;
    require(Worr_PredictionHashCommandV1(&invalid_command) == 0,
            "command hash accepted an invalid size");
    return result;
}

struct command_ring_entry_t {
    uint32_t sequence = 0;
    worr_prediction_command_v1 command{};
};

struct server_record_t {
    worr_prediction_state_v1 state{};
    uint64_t state_hash = 0;
    uint64_t collision_hash = 0;
    worr_prediction_step_v1 output{};
};

correction_result_t run_correction_replay()
{
    constexpr uint32_t kFirstSequence = UINT32_MAX - 4u;
    constexpr size_t kCommandCount = 12;
    constexpr size_t kAcknowledgedIndex = 3;
    static_assert(kCommandCount <= kCommandRingSize);

    std::array<command_ring_entry_t, kCommandRingSize> ring{};
    std::array<server_record_t, kCommandCount> records{};
    const worr_prediction_state_v1 initial_state =
        make_state(0.0f, 0.0f, 24.0f);
    const worr_prediction_config_v1 config = make_config();
    collision_fixture_t authority_bridge_fixture = step_fixture();
    worr_prediction_step_v1 authority_bridge =
        make_step(initial_state, config, &authority_bridge_fixture);
    collision_fixture_t native_authority_fixture = step_fixture();
    native_collision_context_t native_authority_context{};
    native_authority_context.fixture = &native_authority_fixture;
    PMove native_authority = make_native_move(initial_state, config);
    configure_native_step_inputs(native_authority, authority_bridge);

    for (size_t i = 0; i < kCommandCount; ++i) {
        const uint32_t sequence =
            kFirstSequence + static_cast<uint32_t>(i);
        command_ring_entry_t &entry =
            ring[sequence & (kCommandRingSize - 1)];
        entry.sequence = sequence;
        entry.command = make_command(static_cast<uint32_t>(i), 240.0f,
                                     12.0f);
        require(Worr_PredictionCanonicalizeCommandV1(&entry.command),
                "authority command canonicalization failed");
        authority_bridge.command = entry.command;
        require(Worr_PredictionStepV1(&authority_bridge),
                "authoritative ABI reference step rejected");
        run_native_move(native_authority, native_authority_context,
                        entry.command);
        const worr_prediction_state_v1 native_authority_state =
            abi_state_from_native(native_authority.s);
        const uint64_t native_authority_hash =
            Worr_PredictionHashStateV1(&native_authority_state);
        require(equal_full_native_output(authority_bridge,
                                         native_authority,
                                         native_authority_context),
                "correction authority full ABI/native outputs diverged");
        require(equal_state(authority_bridge.state,
                            native_authority_state) &&
                    authority_bridge.state_hash == native_authority_hash,
                "ABI reference/native authority diverged before correction");
        records[i] = {native_authority_state, native_authority_hash,
                      authority_bridge.collision_hash, authority_bridge};
    }

    collision_fixture_t divergent_fixture = step_fixture();
    worr_prediction_state_v1 divergent_state = initial_state;
    divergent_state.origin[1] += 3.0f;
    worr_prediction_step_v1 client =
        make_step(divergent_state, config, &divergent_fixture);
    for (size_t i = 0; i < kCommandCount; ++i) {
        const uint32_t sequence =
            kFirstSequence + static_cast<uint32_t>(i);
        const command_ring_entry_t &entry =
            ring[sequence & (kCommandRingSize - 1)];
        require(entry.sequence == sequence,
                "prediction command ring aliased before correction");
        client.command = entry.command;
        require(Worr_PredictionStepV1(&client),
                "divergent client prediction step rejected");
    }

    correction_result_t result{};
    result.first_sequence = kFirstSequence;
    result.commands = static_cast<uint32_t>(kCommandCount);
    result.pre_correction_hash = client.state_hash;
    require(result.pre_correction_hash != records.back().state_hash,
            "local divergence was not observable before correction");

    result.acknowledged_sequence =
        kFirstSequence + static_cast<uint32_t>(kAcknowledgedIndex);
    result.current_sequence =
        kFirstSequence + static_cast<uint32_t>(kCommandCount - 1);
    require(result.acknowledged_sequence == UINT32_MAX - 1u &&
                result.current_sequence == 6u,
            "correction fixture no longer crosses uint32 wrap");

    client.state = records[kAcknowledgedIndex].state;
    result.authoritative_hash = Worr_PredictionHashStateV1(&client.state);
    require(result.authoritative_hash ==
                records[kAcknowledgedIndex].state_hash,
            "authoritative correction state hash mismatch");

    require(Worr_PredictionReplayCountV1(
                result.acknowledged_sequence, result.current_sequence,
                kCommandRingSize, &result.replayed_commands),
            "production replay plan rejected a valid wrapped range");
    require(result.replayed_commands ==
                kCommandCount - kAcknowledgedIndex - 1,
            "modular replay count is incorrect across uint32 wrap");
    result.sequence_hash = begin_transcript();
    result.replay_collision_hash = begin_transcript();
    result.replay_chain_hash = 0;

    uint32_t rejected_count = UINT32_C(0xdeadbeef);
    require(!Worr_PredictionReplayCountV1(
                result.acknowledged_sequence, result.current_sequence,
                result.replayed_commands, &rejected_count) &&
                rejected_count == UINT32_C(0xdeadbeef),
            "production replay plan accepted a range that fills its ring");

    collision_fixture_t replay_fixture = step_fixture();
    client.collision_context = &replay_fixture;
    for (uint32_t ordinal = 0; ordinal < result.replayed_commands;
         ++ordinal) {
        uint32_t sequence = 0;
        require(Worr_PredictionReplaySequenceV1(
                    result.acknowledged_sequence, result.replayed_commands,
                    ordinal, &sequence),
                "production replay plan failed to index a command");
        const command_ring_entry_t &entry =
            ring[sequence & (kCommandRingSize - 1)];
        require(entry.sequence == sequence,
                "prediction command ring sequence identity mismatch");
        client.command = entry.command;
        require(Worr_PredictionStepV1(&client),
                "corrected client replay step rejected");

        const size_t record_index = kAcknowledgedIndex + 1 + ordinal;
        require(equal_state(client.state, records[record_index].state),
                "corrected replay state fields diverged from authority");
        require(client.state_hash == records[record_index].state_hash,
                "corrected replay state hash diverged from authority");
        require(client.collision_hash ==
                    records[record_index].collision_hash,
                "corrected replay collision transcript diverged");
        require(equal_full_output_values(client,
                                         records[record_index].output,
                                         true),
                "corrected replay full output diverged from authority");
        result.sequence_hash = append_u64(result.sequence_hash, sequence);
        result.replay_collision_hash = append_u64(
            result.replay_collision_hash, client.collision_hash);
        result.replay_chain_hash = Worr_PredictionReplayChainHashV1(
            result.replay_chain_hash, sequence,
            Worr_PredictionHashCommandV1(&entry.command),
            client.collision_hash, client.state_hash);
    }
    uint32_t invalid_sequence = 0;
    require(!Worr_PredictionReplaySequenceV1(
                result.acknowledged_sequence, result.replayed_commands,
                result.replayed_commands, &invalid_sequence),
            "production replay plan accepted an out-of-range index");

    result.final_hash = client.state_hash;
    require(result.final_hash == records.back().state_hash,
            "corrected replay did not converge to authoritative state");
    return result;
}

void print_hash(uint64_t hash)
{
    std::printf("%016llx", static_cast<unsigned long long>(hash));
}

void print_float_value(float value)
{
    std::printf("{\"value\":%.9g,\"bits\":\"%08x\"}",
                static_cast<double>(value),
                std::bit_cast<uint32_t>(value));
}

void print_vec3(const float value[3])
{
    std::printf("[");
    for (size_t i = 0; i < 3; ++i) {
        print_float_value(value[i]);
        if (i != 2)
            std::printf(",");
    }
    std::printf("]");
}

void print_state_json(const worr_prediction_state_v1 &state)
{
    std::printf("{\"struct_size\":%u,\"schema_version\":%u,"
                "\"movement_type\":%d,\"origin\":",
                state.struct_size, state.schema_version,
                state.movement_type);
    print_vec3(state.origin);
    std::printf(",\"velocity\":");
    print_vec3(state.velocity);
    std::printf(",\"movement_flags\":%u,\"movement_time_ms\":%u,"
                "\"gravity\":%d,\"view_height\":%d,\"reserved0\":%u,"
                "\"delta_angles\":",
                state.movement_flags, state.movement_time_ms,
                state.gravity, static_cast<int>(state.view_height),
                state.reserved0);
    print_vec3(state.delta_angles);
    std::printf(",\"semantic_hash\":\"");
    print_hash(Worr_PredictionHashStateV1(&state));
    std::printf("\"}");
}

void print_command_json(const worr_prediction_command_v1 &command)
{
    std::printf("{\"struct_size\":%u,\"schema_version\":%u,"
                "\"duration_ms\":%u,\"buttons\":%u,\"reserved0\":%u,"
                "\"view_angles\":",
                command.struct_size, command.schema_version,
                command.duration_ms, command.buttons, command.reserved0);
    print_vec3(command.view_angles);
    std::printf(",\"forward_move\":");
    print_float_value(command.forward_move);
    std::printf(",\"side_move\":");
    print_float_value(command.side_move);
    std::printf(",\"hash\":\"");
    print_hash(Worr_PredictionHashCommandV1(&command));
    std::printf("\"}");
}

void print_config_json(const worr_prediction_config_v1 &config)
{
    std::printf("{\"struct_size\":%u,\"schema_version\":%u,"
                "\"movement_model_revision\":%u,"
                "\"air_acceleration\":%d,\"flags\":%u,\"hash\":\"",
                config.struct_size, config.schema_version,
                config.movement_model_revision, config.air_acceleration,
                config.flags);
    print_hash(Worr_PredictionHashConfigV1(&config));
    std::printf("\"}");
}

void print_plane_json(const worr_prediction_plane_v1 &plane)
{
    std::printf("{\"normal\":");
    print_vec3(plane.normal);
    std::printf(",\"distance\":");
    print_float_value(plane.distance);
    std::printf(",\"type\":%u,\"sign_bits\":%u,"
                "\"reserved\":[%u,%u]}",
                plane.type, plane.sign_bits, plane.reserved[0],
                plane.reserved[1]);
}

void print_step_output_json(const worr_prediction_step_v1 &step,
                            bool native_output)
{
    std::printf("{\"state\":");
    print_state_json(step.state);
    std::printf(",\"state_hash\":\"");
    print_hash(step.state_hash);
    std::printf("\",\"view_angles\":");
    print_vec3(step.view_angles);
    std::printf(",\"mins\":");
    print_vec3(step.mins);
    std::printf(",\"maxs\":");
    print_vec3(step.maxs);
    std::printf(",\"ground_plane\":");
    print_plane_json(step.ground_plane);
    std::printf(",\"ground_entity_id\":%u,\"water_type\":%u,"
                "\"water_level\":%u,\"rd_flags\":%u,"
                "\"jump_sound\":%u,\"step_clip\":%u,"
                "\"impact_delta\":",
                step.ground_entity_id, step.water_type, step.water_level,
                step.rd_flags, step.jump_sound, step.step_clip);
    print_float_value(step.impact_delta);
    std::printf(",\"screen_blend\":[");
    for (size_t i = 0; i < 4; ++i) {
        print_float_value(step.screen_blend[i]);
        if (i != 3)
            std::printf(",");
    }
    std::printf("],\"touch_count\":%u,\"touch_entity_ids\":[",
                step.touch_count);
    for (size_t i = 0; i < WORR_PREDICTION_MAX_TOUCH; ++i) {
        std::printf("%u", step.touch_entity_ids[i]);
        if (i + 1 != WORR_PREDICTION_MAX_TOUCH)
            std::printf(",");
    }
    std::printf("],\"collision_query_count\":%u,"
                "\"collision_hash\":",
                step.collision_query_count);
    if (native_output) {
        std::printf("null");
    } else {
        std::printf("\"");
        print_hash(step.collision_hash);
        std::printf("\"");
    }
    std::printf(",\"reserved1\":%u}", step.reserved1);
}

void print_focused_fixture_json(const focused_fixture_result_t &fixture)
{
    std::printf("{\"name\":\"%s\",\"category\":\"%s\","
                "\"initial_state\":",
                fixture.name, fixture.category);
    print_state_json(fixture.initial_state);
    std::printf(",\"canonical_command\":");
    print_command_json(fixture.canonical_command);
    std::printf(",\"config\":");
    print_config_json(fixture.config);
    std::printf(",\"view_offset\":");
    print_vec3(fixture.output.view_offset);
    std::printf(",\"snap_initial\":%u,\"player_entity_id\":%u,"
                "\"output\":",
                fixture.snap_initial, fixture.player_entity_id);
    print_step_output_json(fixture.output, false);
    std::printf(",\"native_output\":");
    print_step_output_json(fixture.native_output, true);
    std::printf(",\"full_output_equal\":%s,\"control\":",
                fixture.full_output_equal ? "true" : "false");
    if (!fixture.has_control) {
        std::printf("null");
    } else {
        std::printf("{\"initial_state\":");
        print_state_json(fixture.control_initial_state);
        std::printf(",\"canonical_command\":");
        print_command_json(fixture.control_command);
        std::printf(",\"config\":");
        print_config_json(fixture.control_config);
        std::printf(",\"view_offset\":");
        print_vec3(fixture.control_output.view_offset);
        std::printf(",\"snap_initial\":%u,"
                    "\"player_entity_id\":%u,\"output\":",
                    fixture.control_snap_initial,
                    fixture.control_output.player_entity_id);
        print_step_output_json(fixture.control_output, false);
        std::printf(",\"native_output\":");
        print_step_output_json(fixture.native_control_output, true);
        std::printf(",\"full_output_equal\":%s}",
                    fixture.control_full_output_equal ? "true" : "false");
    }
    std::printf(",\"behaviour_assertions\":{");
    for (size_t i = 0; i < fixture.behaviour_assertion_count; ++i) {
        std::printf("\"%s\":true", fixture.behaviour_assertions[i]);
        if (i + 1 != fixture.behaviour_assertion_count)
            std::printf(",");
    }
    std::printf("}}");
}

void print_json(const hash_contract_result_t &hashes,
                const std::array<scenario_result_t, 5> &scenarios,
                const std::array<coverage_result_t, kCoverageCaseCount>
                    &coverage,
                const correction_result_t &correction,
                const nested_step_result_t &nested,
                const fail_closed_result_t &fail_closed,
                const std::array<focused_fixture_result_t,
                                 kFocusedFixtureCount> &focused)
{
    std::printf("{\n");
    std::printf("  \"schema\": \"worr.networking.prediction-parity.v1\",\n");
    std::printf("  \"abi_version\": %u,\n", WORR_PREDICTION_ABI_VERSION);
    std::printf("  \"movement_model_revision\": %u,\n",
                WORR_PREDICTION_MODEL_REVISION);
    std::printf("  \"parity_scope\": "
                "\"abi-client-vs-native-server-core\",\n");
    std::printf("  \"full_output_scope\": "
                "\"all-pmove-value-outputs-excluding-native-collision-hash\",\n");
    std::printf("  \"wire_parity_scope\": "
                "\"not-covered-by-this-harness\",\n");
    std::printf("  \"bridge_repeatability\": true,\n");
    std::printf("  \"full_output_parity_all_cases\": true,\n");
    std::printf("  \"nested_step\": {\n");
    std::printf("    \"outer_state\": \"");
    print_hash(nested.outer_state_hash);
    std::printf("\",\n    \"outer_collision\": \"");
    print_hash(nested.outer_collision_hash);
    std::printf("\",\n    \"inner_state\": \"");
    print_hash(nested.inner_state_hash);
    std::printf("\",\n    \"inner_collision\": \"");
    print_hash(nested.inner_collision_hash);
    std::printf("\"\n  },\n");
    std::printf("  \"hash_contract\": {\n");
    std::printf("    \"state\": \"");
    print_hash(hashes.state_hash);
    std::printf("\",\n    \"command\": \"");
    print_hash(hashes.command_hash);
    std::printf("\",\n    \"config\": \"");
    print_hash(hashes.config_hash);
    std::printf("\"\n  },\n");
    std::printf("  \"scenarios\": [\n");
    for (size_t i = 0; i < scenarios.size(); ++i) {
        const scenario_result_t &scenario = scenarios[i];
        std::printf("    {\"name\": \"%s\", \"command_count\": %u, "
                    "\"command_transcript\": \"",
                    scenario.name, scenario.command_count);
        print_hash(scenario.command_hash);
        std::printf("\", \"final_state\": \"");
        print_hash(scenario.state_hash);
        std::printf("\", \"native_server_state\": \"");
        print_hash(scenario.native_server_state_hash);
        std::printf("\", \"state_transcript\": \"");
        print_hash(scenario.state_transcript_hash);
        std::printf("\", \"collision_transcript\": \"");
        print_hash(scenario.collision_transcript_hash);
        std::printf("\", \"replay_chain\": \"");
        print_hash(scenario.replay_chain_hash);
        std::printf("\", \"collision_queries\": %llu, "
                    "\"full_output_parity\": %s}",
                    static_cast<unsigned long long>(
                        scenario.collision_queries),
                    scenario.full_output_parity ? "true" : "false");
        std::printf(i + 1 == scenarios.size() ? "\n" : ",\n");
    }
    std::printf("  ],\n");
    std::printf("  \"coverage_cases\": [\n");
    for (size_t i = 0; i < coverage.size(); ++i) {
        const coverage_result_t &item = coverage[i];
        std::printf("    {\"name\": \"%s\", \"category\": \"%s\", "
                    "\"command\": \"",
                    item.name, item.category);
        print_hash(item.command_hash);
        std::printf("\", \"config\": \"");
        print_hash(item.config_hash);
        std::printf("\", \"final_state\": \"");
        print_hash(item.state_hash);
        std::printf("\", \"native_server_state\": \"");
        print_hash(item.native_server_state_hash);
        std::printf("\", \"collision\": \"");
        print_hash(item.collision_hash);
        std::printf("\", \"collision_queries\": %u, "
                    "\"full_output_parity\": %s}",
                    item.collision_queries,
                    item.full_output_parity ? "true" : "false");
        std::printf(i + 1 == coverage.size() ? "\n" : ",\n");
    }
    std::printf("  ],\n");
    std::printf("  \"focused_fixtures\": [\n");
    for (size_t i = 0; i < focused.size(); ++i) {
        std::printf("    ");
        print_focused_fixture_json(focused[i]);
        std::printf(i + 1 == focused.size() ? "\n" : ",\n");
    }
    std::printf("  ],\n");
    std::printf("  \"fail_closed\": {\n");
    std::printf("    \"passed_cases\": %u,\n",
                fail_closed.passed_cases);
    std::printf("    \"case_names\": [");
    for (size_t i = 0; i < fail_closed.passed_cases; ++i) {
        std::printf("\"%s\"", fail_closed.case_names[i]);
        if (i + 1 != fail_closed.passed_cases)
            std::printf(", ");
    }
    std::printf("],\n    \"transcript\": \"");
    print_hash(fail_closed.transcript_hash);
    std::printf("\"\n  },\n");
    std::printf("  \"correction_replay\": {\n");
    std::printf("    \"first_sequence\": %u,\n",
                correction.first_sequence);
    std::printf("    \"acknowledged_sequence\": %u,\n",
                correction.acknowledged_sequence);
    std::printf("    \"current_sequence\": %u,\n",
                correction.current_sequence);
    std::printf("    \"commands\": %u,\n",
                correction.commands);
    std::printf("    \"replayed_commands\": %u,\n",
                correction.replayed_commands);
    std::printf("    \"pre_correction_state\": \"");
    print_hash(correction.pre_correction_hash);
    std::printf("\",\n    \"authoritative_state\": \"");
    print_hash(correction.authoritative_hash);
    std::printf("\",\n    \"final_state\": \"");
    print_hash(correction.final_hash);
    std::printf("\",\n    \"sequence_transcript\": \"");
    print_hash(correction.sequence_hash);
    std::printf("\",\n    \"collision_transcript\": \"");
    print_hash(correction.replay_collision_hash);
    std::printf("\",\n    \"replay_chain\": \"");
    print_hash(correction.replay_chain_hash);
    std::printf("\"\n  }\n}\n");
}

} // namespace

int main(int argc, char **argv)
{
    bool json = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--json")
            json = true;
        else
            fail("unknown command-line argument");
    }

    const hash_contract_result_t hashes = validate_hash_contract();
    const std::array<scenario_spec_t, 5> specs = make_scenarios();
    std::array<scenario_result_t, 5> scenarios{};
    for (size_t i = 0; i < scenarios.size(); ++i)
        scenarios[i] = run_scenario(specs[i]);
    validate_scenario_behaviour(scenarios);
    const std::array<coverage_spec_t, kCoverageCaseCount> coverage_specs =
        make_coverage_specs();
    std::array<coverage_result_t, kCoverageCaseCount> coverage{};
    for (size_t i = 0; i < coverage.size(); ++i)
        coverage[i] = run_coverage_case(coverage_specs[i]);
    validate_coverage_relationships(coverage);
    const correction_result_t correction = run_correction_replay();
    const nested_step_result_t nested = validate_nested_step();
    const std::array<focused_fixture_result_t, kFocusedFixtureCount>
        focused = run_focused_fixtures();
    const fail_closed_result_t fail_closed = validate_fail_closed_cases();

    if (json)
        print_json(hashes, scenarios, coverage, correction, nested,
                   fail_closed, focused);
    else
        std::puts("prediction ABI replay parity tests passed");
    return EXIT_SUCCESS;
}
