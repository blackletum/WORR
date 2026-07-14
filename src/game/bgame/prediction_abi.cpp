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
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

namespace {

static_assert(sizeof(float) == sizeof(uint32_t));
static_assert(std::numeric_limits<float>::is_iec559);
static_assert(std::is_standard_layout_v<worr_prediction_state_v1>);
static_assert(std::is_standard_layout_v<worr_prediction_command_v1>);
static_assert(std::is_standard_layout_v<worr_prediction_config_v1>);
static_assert(sizeof(worr_prediction_state_v1) == 56);
static_assert(sizeof(worr_prediction_command_v1) == 32);
static_assert(sizeof(worr_prediction_config_v1) == 20);
static_assert(sizeof(worr_prediction_plane_v1) == 20);
static_assert(sizeof(worr_prediction_trace_v1) == 92);

constexpr size_t kMaxTraceSurfaces = 512;
constexpr size_t kMaxEntityTokens = 512;
constexpr uint8_t kPlaneNonAxial = 6;

constexpr uint32_t kKnownConfigFlags =
    WORR_PREDICTION_CONFIG_N64_PHYSICS |
    WORR_PREDICTION_CONFIG_Q3_OVERBOUNCE;
constexpr uint16_t kKnownMovementFlags =
    PMF_DUCKED | PMF_JUMP_HELD | PMF_ON_GROUND | PMF_TIME_WATERJUMP |
    PMF_TIME_LAND | PMF_TIME_KNOCKBACK |
    PMF_NO_POSITIONAL_PREDICTION | PMF_ON_LADDER |
    PMF_NO_ANGULAR_PREDICTION | PMF_IGNORE_PLAYER_COLLISION |
    PMF_TIME_TRICK | PMF_HASTE | PMF_TIME_SPAWN_LOCK |
    PMF_LEGACY_TELEPORT_BIT;

uint64_t append_u8(uint64_t hash, uint8_t value);
uint64_t append_u32(uint64_t hash, uint32_t value);
uint64_t append_u64(uint64_t hash, uint64_t value);
uint64_t begin_hash(uint32_t domain);
uint64_t append_vec(uint64_t hash, const float value[3]);
uint32_t canonical_float_bits(float value);

struct alignas(std::max_align_t) entity_token_storage_t {
    uint32_t id = WORR_PREDICTION_NO_ENTITY;
};

struct prediction_bridge_t {
    void *collision_context = nullptr;
    worr_prediction_trace_fn_v1 trace = nullptr;
    worr_prediction_point_contents_fn_v1 point_contents = nullptr;
    std::array<csurface_t, kMaxTraceSurfaces> surfaces{};
    std::array<uint32_t, kMaxTraceSurfaces> surface_ids{};
    size_t surface_count = 0;
    std::array<entity_token_storage_t, kMaxEntityTokens> entities{};
    size_t entity_count = 0;
    uint64_t collision_hash = 0;
    uint32_t collision_query_count = 0;
    bool valid = true;
};

thread_local prediction_bridge_t *active_bridge = nullptr;

class active_bridge_scope_t {
public:
    explicit active_bridge_scope_t(prediction_bridge_t *bridge) noexcept
        : previous_(active_bridge)
    {
        active_bridge = bridge;
    }

    ~active_bridge_scope_t() noexcept
    {
        active_bridge = previous_;
    }

    active_bridge_scope_t(const active_bridge_scope_t &) = delete;
    active_bridge_scope_t &operator=(const active_bridge_scope_t &) = delete;

private:
    prediction_bridge_t *previous_;
};

bool valid_float(float value)
{
    return std::isfinite(value);
}

bool valid_vec(const float value[3])
{
    return valid_float(value[0]) && valid_float(value[1]) &&
           valid_float(value[2]);
}

Vector3 native_vec(const float value[3])
{
    return {value[0], value[1], value[2]};
}

void abi_vec(float out[3], const Vector3 &value)
{
    out[0] = value[0];
    out[1] = value[1];
    out[2] = value[2];
}

void native_plane(cplane_t &out, const worr_prediction_plane_v1 &value)
{
    out.normal = native_vec(value.normal);
    out.dist = value.distance;
    out.type = value.type;
    out.signBits = value.sign_bits;
    out.pad[0] = out.pad[1] = 0;
}

void abi_plane(worr_prediction_plane_v1 &out, const cplane_t &value)
{
    abi_vec(out.normal, value.normal);
    out.distance = value.dist;
    out.type = value.type;
    out.sign_bits = value.signBits;
    out.reserved[0] = out.reserved[1] = 0;
}

gentity_t *entity_token(uint32_t id)
{
    if (id == WORR_PREDICTION_NO_ENTITY)
        return nullptr;
    if (!active_bridge || !active_bridge->valid)
        return nullptr;

    for (size_t i = 0; i < active_bridge->entity_count; ++i) {
        if (active_bridge->entities[i].id == id) {
            return reinterpret_cast<gentity_t *>(
                &active_bridge->entities[i]);
        }
    }

    if (active_bridge->entity_count >= active_bridge->entities.size()) {
        active_bridge->valid = false;
        return nullptr;
    }

    entity_token_storage_t &token =
        active_bridge->entities[active_bridge->entity_count++];
    token.id = id;
    return reinterpret_cast<gentity_t *>(&token);
}

uint32_t entity_id(const gentity_t *token)
{
    if (!token)
        return WORR_PREDICTION_NO_ENTITY;
    if (!active_bridge || !active_bridge->valid)
        return WORR_PREDICTION_NO_ENTITY;

    for (size_t i = 0; i < active_bridge->entity_count; ++i) {
        if (token == reinterpret_cast<const gentity_t *>(
                         &active_bridge->entities[i])) {
            return active_bridge->entities[i].id;
        }
    }

    active_bridge->valid = false;
    return WORR_PREDICTION_NO_ENTITY;
}

csurface_t *make_surface(uint32_t id, uint32_t flags)
{
    if (id == WORR_PREDICTION_NO_ENTITY)
        return nullptr;

    if (!active_bridge || !active_bridge->valid)
        return nullptr;

    for (size_t i = 0; i < active_bridge->surface_count; ++i) {
        if (active_bridge->surface_ids[i] != id)
            continue;
        if (static_cast<uint32_t>(active_bridge->surfaces[i].flags) != flags)
            active_bridge->valid = false;
        return &active_bridge->surfaces[i];
    }

    if (active_bridge->surface_count >= active_bridge->surfaces.size()) {
        if (active_bridge)
            active_bridge->valid = false;
        return nullptr;
    }

    const size_t index = active_bridge->surface_count++;
    active_bridge->surface_ids[index] = id;
    csurface_t &surface = active_bridge->surfaces[index];
    surface = {};
    surface.id = id;
    surface.flags = static_cast<surfflags_t>(flags);
    return &surface;
}

trace_t invalid_trace(gvec3_cref_t start)
{
    trace_t trace{};
    trace.allSolid = true;
    trace.startSolid = true;
    trace.fraction = 0.0f;
    trace.endPos = start;
    return trace;
}

trace_t bridge_trace(gvec3_cref_t start, gvec3_cptr_t mins,
                     gvec3_cptr_t maxs, gvec3_cref_t end,
                     const gentity_t *passent, contents_t mask,
                     uint32_t query_flags)
{
    if (!active_bridge || !active_bridge->valid || !active_bridge->trace) {
        return invalid_trace(start);
    }

    const Vector3 zero{};
    const Vector3 &query_mins = mins ? *mins : zero;
    const Vector3 &query_maxs = maxs ? *maxs : zero;
    worr_prediction_trace_v1 result{};
    result.struct_size = sizeof(result);
    result.schema_version = WORR_PREDICTION_ABI_VERSION;
    result.fraction = 1.0f;
    abi_vec(result.end, end);
    result.surface_id = WORR_PREDICTION_NO_ENTITY;
    result.surface2_id = WORR_PREDICTION_NO_ENTITY;
    result.entity_id = WORR_PREDICTION_NO_ENTITY;

    float start_value[3], mins_value[3], maxs_value[3], end_value[3];
    abi_vec(start_value, start);
    abi_vec(mins_value, query_mins);
    abi_vec(maxs_value, query_maxs);
    abi_vec(end_value, end);

    uint64_t transcript = active_bridge->collision_hash;
    transcript = append_u32(transcript, UINT32_C(0x54524143)); // CART
    transcript = append_u32(transcript,
                            active_bridge->collision_query_count++);
    transcript = append_vec(transcript, start_value);
    transcript = append_vec(transcript, mins_value);
    transcript = append_vec(transcript, maxs_value);
    transcript = append_vec(transcript, end_value);
    transcript = append_u32(transcript, entity_id(passent));
    transcript = append_u32(transcript, static_cast<uint32_t>(mask));
    transcript = append_u32(transcript, query_flags);

    active_bridge->trace(
        active_bridge->collision_context, &result, start_value,
        mins_value, maxs_value, end_value, entity_id(passent),
        static_cast<uint32_t>(mask), query_flags);

    if (result.struct_size != sizeof(result) ||
        result.schema_version != WORR_PREDICTION_ABI_VERSION ||
        result.all_solid > 1 || result.start_solid > 1 ||
        result.has_second_surface > 1 || result.reserved0 != 0 ||
        result.plane.reserved[0] != 0 || result.plane.reserved[1] != 0 ||
        !valid_float(result.fraction) || result.fraction < 0.0f ||
        result.fraction > 1.0f || !valid_vec(result.end) ||
        !valid_vec(result.plane.normal) ||
        !valid_float(result.plane.distance) ||
        result.plane.type > kPlaneNonAxial || result.plane.sign_bits > 7 ||
        (result.surface_id == WORR_PREDICTION_NO_ENTITY &&
         result.surface_flags != 0) ||
        (result.has_second_surface &&
         (result.plane2.reserved[0] != 0 ||
          result.plane2.reserved[1] != 0 ||
          !valid_vec(result.plane2.normal) ||
          !valid_float(result.plane2.distance) ||
          result.plane2.type > kPlaneNonAxial ||
          result.plane2.sign_bits > 7 ||
          result.surface2_id == WORR_PREDICTION_NO_ENTITY))) {
        active_bridge->valid = false;
        return invalid_trace(start);
    }

    if (!result.has_second_surface) {
        result.plane2 = {};
        result.surface2_flags = 0;
        result.surface2_id = WORR_PREDICTION_NO_ENTITY;
    }

    transcript = append_u32(transcript, result.all_solid);
    transcript = append_u32(transcript, result.start_solid);
    transcript = append_u32(transcript, result.has_second_surface);
    transcript = append_u32(transcript, canonical_float_bits(result.fraction));
    transcript = append_vec(transcript, result.end);
    transcript = append_vec(transcript, result.plane.normal);
    transcript = append_u32(
        transcript, canonical_float_bits(result.plane.distance));
    transcript = append_u32(transcript, result.plane.type);
    transcript = append_u32(transcript, result.plane.sign_bits);
    transcript = append_u32(transcript, result.surface_flags);
    transcript = append_u32(transcript, result.surface_id);
    transcript = append_u32(transcript, result.contents);
    transcript = append_u32(transcript, result.entity_id);
    transcript = append_vec(transcript, result.plane2.normal);
    transcript = append_u32(
        transcript, canonical_float_bits(result.plane2.distance));
    transcript = append_u32(transcript, result.plane2.type);
    transcript = append_u32(transcript, result.plane2.sign_bits);
    transcript = append_u32(transcript, result.surface2_flags);
    transcript = append_u32(transcript, result.surface2_id);
    active_bridge->collision_hash = transcript;

    trace_t trace{};
    trace.allSolid = result.all_solid != 0;
    trace.startSolid = result.start_solid != 0;
    trace.fraction = result.fraction;
    trace.endPos = native_vec(result.end);
    native_plane(trace.plane, result.plane);
    trace.surface = make_surface(result.surface_id, result.surface_flags);
    trace.contents = static_cast<contents_t>(result.contents);
    trace.ent = entity_token(result.entity_id);
    native_plane(trace.plane2, result.plane2);
    if (result.has_second_surface) {
        trace.surface2 =
            make_surface(result.surface2_id, result.surface2_flags);
    }
    return trace;
}

trace_t trace_all(gvec3_cref_t start, gvec3_cptr_t mins,
                  gvec3_cptr_t maxs, gvec3_cref_t end,
                  const gentity_t *passent, contents_t mask)
{
    return bridge_trace(start, mins, maxs, end, passent, mask, 0);
}

trace_t trace_world(gvec3_cref_t start, gvec3_cptr_t mins,
                    gvec3_cptr_t maxs, gvec3_cref_t end, contents_t mask)
{
    return bridge_trace(start, mins, maxs, end, nullptr, mask,
                        WORR_PREDICTION_TRACE_WORLD_ONLY);
}

contents_t point_contents(gvec3_cref_t point)
{
    if (!active_bridge || !active_bridge->valid ||
        !active_bridge->point_contents) {
        if (active_bridge)
            active_bridge->valid = false;
        return CONTENTS_NONE;
    }
    float value[3];
    abi_vec(value, point);
    const uint32_t contents = active_bridge->point_contents(
        active_bridge->collision_context, value);
    uint64_t transcript = active_bridge->collision_hash;
    transcript = append_u32(transcript, UINT32_C(0x504f494e)); // NIOP
    transcript = append_u32(transcript,
                            active_bridge->collision_query_count++);
    transcript = append_vec(transcript, value);
    active_bridge->collision_hash = append_u32(transcript, contents);
    return static_cast<contents_t>(contents);
}

contents_t presentation_point_contents(gvec3_cref_t point)
{
    if (!active_bridge || !active_bridge->valid ||
        !active_bridge->point_contents) {
        if (active_bridge)
            active_bridge->valid = false;
        return CONTENTS_NONE;
    }

    float value[3];
    abi_vec(value, point);
    return static_cast<contents_t>(active_bridge->point_contents(
        active_bridge->collision_context, value));
}

bool valid_header(uint32_t size, uint32_t version, size_t expected)
{
    return size == expected && version == WORR_PREDICTION_ABI_VERSION;
}

bool valid_state(const worr_prediction_state_v1 &state)
{
    return valid_header(state.struct_size, state.schema_version,
                        sizeof(state)) &&
           state.movement_type >= PM_NORMAL &&
           state.movement_type <= PM_FREEZE &&
           !(state.movement_flags & ~kKnownMovementFlags) &&
           state.reserved0 == 0 && valid_vec(state.origin) &&
           valid_vec(state.velocity) && valid_vec(state.delta_angles);
}

bool canonical_angle(float value, float &canonical)
{
    return NetUsercmd_CanonicalizeAngle(value, &canonical);
}

bool canonical_move(float value, float &canonical)
{
    return NetUsercmd_CanonicalizeMove(value, &canonical);
}

bool canonical_command(const worr_prediction_command_v1 &input,
                       worr_prediction_command_v1 &canonical)
{
    if (!valid_header(input.struct_size, input.schema_version,
                      sizeof(input)) ||
        input.reserved0 != 0) {
        return false;
    }

    canonical = input;
    canonical.reserved0 = 0;
    for (unsigned i = 0; i < 3; ++i) {
        if (!canonical_angle(input.view_angles[i], canonical.view_angles[i]))
            return false;
    }
    return canonical_move(input.forward_move, canonical.forward_move) &&
           canonical_move(input.side_move, canonical.side_move);
}

bool valid_config(const worr_prediction_config_v1 &config)
{
    return valid_header(config.struct_size, config.schema_version,
                        sizeof(config)) &&
           config.movement_model_revision ==
               WORR_PREDICTION_MODEL_REVISION &&
           !(config.flags & ~kKnownConfigFlags);
}

bool valid_step_input(const worr_prediction_step_v1 &step,
                      worr_prediction_command_v1 &canonical)
{
    return valid_header(step.struct_size, step.schema_version, sizeof(step)) &&
           valid_state(step.state) &&
           canonical_command(step.command, canonical) &&
           valid_config(step.config) && step.snap_initial <= 1 &&
           step.reserved0[0] == 0 && step.reserved0[1] == 0 &&
           step.reserved0[2] == 0 && valid_vec(step.view_offset) &&
           step.trace && step.point_contents;
}

void native_state(pmove_state_t &out, const worr_prediction_state_v1 &value)
{
    out = {};
    out.pmType = static_cast<pmtype_t>(value.movement_type);
    out.origin = native_vec(value.origin);
    out.velocity = native_vec(value.velocity);
    out.pmFlags = static_cast<pmflags_t>(value.movement_flags);
    out.pmTime = value.movement_time_ms;
    out.gravity = value.gravity;
    out.viewHeight = value.view_height;
    out.deltaAngles = native_vec(value.delta_angles);
}

void abi_state(worr_prediction_state_v1 &out, const pmove_state_t &value)
{
    out = {};
    out.struct_size = sizeof(out);
    out.schema_version = WORR_PREDICTION_ABI_VERSION;
    out.movement_type = static_cast<int32_t>(value.pmType);
    abi_vec(out.origin, value.origin);
    abi_vec(out.velocity, value.velocity);
    out.movement_flags = static_cast<uint16_t>(value.pmFlags);
    out.movement_time_ms = value.pmTime;
    out.gravity = value.gravity;
    out.view_height = value.viewHeight;
    abi_vec(out.delta_angles, value.deltaAngles);
}

void native_command(usercmd_t &out,
                    const worr_prediction_command_v1 &value)
{
    out = {};
    out.msec = value.duration_ms;
    out.buttons = static_cast<button_t>(value.buttons);
    out.angles = native_vec(value.view_angles);
    out.forwardMove = value.forward_move;
    out.sideMove = value.side_move;
}

bool valid_native_vec(const Vector3 &value)
{
    return valid_float(value[0]) && valid_float(value[1]) &&
           valid_float(value[2]);
}

bool valid_native_plane(const cplane_t &plane)
{
    return valid_native_vec(plane.normal) && valid_float(plane.dist) &&
           plane.type <= kPlaneNonAxial && plane.signBits <= 7;
}

bool valid_native_output(const PMove &move)
{
    constexpr uint8_t known_rd_flags =
        RDF_UNDERWATER | RDF_NOWORLDMODEL | RDF_IRGOGGLES | RDF_UVGOGGLES |
        RDF_NO_WEAPON_LERP;

    if (move.s.pmType < PM_NORMAL || move.s.pmType > PM_FREEZE ||
        (static_cast<uint16_t>(move.s.pmFlags) & ~kKnownMovementFlags) ||
        !valid_native_vec(move.s.origin) ||
        !valid_native_vec(move.s.velocity) ||
        !valid_native_vec(move.s.deltaAngles) ||
        !valid_native_vec(move.viewAngles) || !valid_native_vec(move.mins) ||
        !valid_native_vec(move.maxs) ||
        !valid_native_plane(move.groundPlane) ||
        move.waterLevel > WATER_UNDER ||
        (static_cast<uint8_t>(move.rdFlags) & ~known_rd_flags) ||
        !valid_float(move.impactDelta) || move.touch.num > MAXTOUCH) {
        return false;
    }

    for (float component : move.screenBlend) {
        if (!valid_float(component))
            return false;
    }
    return true;
}

uint32_t canonical_float_bits(float value)
{
    uint32_t bits = std::bit_cast<uint32_t>(value);
    if ((bits & UINT32_C(0x7fffffff)) == 0)
        return 0;
    if ((bits & UINT32_C(0x7f800000)) == UINT32_C(0x7f800000) &&
        (bits & UINT32_C(0x007fffff))) {
        return UINT32_C(0x7fc00000);
    }
    return bits;
}

uint64_t append_u8(uint64_t hash, uint8_t value)
{
    hash ^= value;
    return hash * UINT64_C(1099511628211);
}

uint64_t append_u32(uint64_t hash, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        hash = append_u8(hash, static_cast<uint8_t>(value >> (i * 8u)));
    return hash;
}

uint64_t append_u64(uint64_t hash, uint64_t value)
{
    for (unsigned i = 0; i < 8; ++i)
        hash = append_u8(hash, static_cast<uint8_t>(value >> (i * 8u)));
    return hash;
}

uint64_t begin_hash(uint32_t domain)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    hash = append_u32(hash, UINT32_C(0x52524f57)); // WORR, little endian
    hash = append_u32(hash, WORR_PREDICTION_ABI_VERSION);
    return append_u32(hash, domain);
}

uint64_t append_vec(uint64_t hash, const float value[3])
{
    for (unsigned i = 0; i < 3; ++i)
        hash = append_u32(hash, canonical_float_bits(value[i]));
    return hash;
}

} // namespace

extern "C" bool Worr_PredictionStepV1(worr_prediction_step_v1 *step)
{
    try {
        if (!step)
            return false;

        const worr_prediction_step_v1 input = *step;
        worr_prediction_command_v1 canonical{};
        if (!valid_step_input(input, canonical))
            return false;

        prediction_bridge_t bridge{};
        bridge.collision_context = input.collision_context;
        bridge.trace = input.trace;
        bridge.point_contents = input.point_contents;
        bridge.collision_hash = begin_hash(UINT32_C(0x434f4c4c)); // LLOC
        active_bridge_scope_t bridge_scope(&bridge);

        PMove move{};
        native_state(move.s, input.state);
        native_command(move.cmd, canonical);
        move.config.airAccel = input.config.air_acceleration;
        move.config.n64Physics =
            (input.config.flags & WORR_PREDICTION_CONFIG_N64_PHYSICS) != 0;
        move.config.q3Overbounce =
            (input.config.flags & WORR_PREDICTION_CONFIG_Q3_OVERBOUNCE) != 0;
        move.snapInitial = input.snap_initial != 0;
        move.player = entity_token(input.player_entity_id);
        move.trace = trace_all;
        move.clip = trace_world;
        move.pointContents = point_contents;
        move.presentationPointContents = presentation_point_contents;
        move.viewOffset = native_vec(input.view_offset);

        Pmove(&move);
        if (!bridge.valid || !valid_native_output(move))
            return false;

        worr_prediction_step_v1 output = input;
        output.command = canonical;
        abi_state(output.state, move.s);
        abi_vec(output.view_angles, move.viewAngles);
        abi_vec(output.mins, move.mins);
        abi_vec(output.maxs, move.maxs);
        abi_plane(output.ground_plane, move.groundPlane);
        output.ground_entity_id = entity_id(move.groundEntity);
        output.water_type = static_cast<uint32_t>(move.waterType);
        output.water_level = static_cast<uint8_t>(move.waterLevel);
        output.rd_flags = static_cast<uint8_t>(move.rdFlags);
        output.jump_sound = move.jumpSound ? 1 : 0;
        output.step_clip = move.stepClip ? 1 : 0;
        output.impact_delta = move.impactDelta;
        for (unsigned i = 0; i < 4; ++i)
            output.screen_blend[i] = move.screenBlend[i];
        output.touch_count =
            std::min<uint32_t>(move.touch.num, WORR_PREDICTION_MAX_TOUCH);
        for (uint32_t i = 0; i < output.touch_count; ++i)
            output.touch_entity_ids[i] = entity_id(move.touch.traces[i].ent);
        for (uint32_t i = output.touch_count;
             i < WORR_PREDICTION_MAX_TOUCH; ++i) {
            output.touch_entity_ids[i] = WORR_PREDICTION_NO_ENTITY;
        }
        output.collision_query_count = bridge.collision_query_count;
        output.reserved1 = 0;
        output.state_hash = Worr_PredictionHashStateV1(&output.state);
        output.collision_hash = append_u32(
            append_u32(bridge.collision_hash, UINT32_C(0x454e4421)),
            bridge.collision_query_count); // "!DNE" + transcript length
        if (!bridge.valid)
            return false;
        *step = output;
        return true;
    } catch (...) {
        /* Never permit a C++ movement/callback exception to cross the C ABI. */
        return false;
    }
}

extern "C" bool Worr_PredictionCanonicalizeCommandV1(
    worr_prediction_command_v1 *command)
{
    if (!command)
        return false;

    worr_prediction_command_v1 canonical{};
    if (!canonical_command(*command, canonical))
        return false;
    *command = canonical;
    return true;
}

extern "C" uint64_t Worr_PredictionHashStateV1(
    const worr_prediction_state_v1 *state)
{
    if (!state || !valid_state(*state)) {
        return 0;
    }
    uint64_t hash = begin_hash(UINT32_C(0x53544154)); // TATS
    hash = append_u32(hash, static_cast<uint32_t>(state->movement_type));
    hash = append_vec(hash, state->origin);
    hash = append_vec(hash, state->velocity);
    hash = append_u32(hash, state->movement_flags);
    hash = append_u32(hash, state->movement_time_ms);
    hash = append_u32(hash, static_cast<uint16_t>(state->gravity));
    hash = append_u32(hash, static_cast<uint8_t>(state->view_height));
    return append_vec(hash, state->delta_angles);
}

extern "C" uint64_t Worr_PredictionHashCommandV1(
    const worr_prediction_command_v1 *command)
{
    if (!command) {
        return 0;
    }

    worr_prediction_command_v1 canonical{};
    if (!canonical_command(*command, canonical))
        return 0;

    uint64_t hash = begin_hash(UINT32_C(0x434d4420)); // " DMC"
    hash = append_u32(hash, canonical.duration_ms);
    hash = append_u32(hash, canonical.buttons);
    hash = append_vec(hash, canonical.view_angles);
    hash = append_u32(hash, canonical_float_bits(canonical.forward_move));
    return append_u32(hash, canonical_float_bits(canonical.side_move));
}

extern "C" uint64_t Worr_PredictionHashConfigV1(
    const worr_prediction_config_v1 *config)
{
    if (!config || !valid_config(*config)) {
        return 0;
    }
    uint64_t hash = begin_hash(UINT32_C(0x43464720)); // " GFC"
    hash = append_u32(hash, config->movement_model_revision);
    hash = append_u32(hash, static_cast<uint32_t>(config->air_acceleration));
    return append_u32(hash, config->flags);
}

extern "C" uint64_t Worr_PredictionReplayChainHashV1(
    uint64_t previous_chain_hash, uint32_t command_sequence,
    uint64_t command_hash, uint64_t collision_hash, uint64_t state_hash)
{
    uint64_t hash = begin_hash(UINT32_C(0x43484149)); // "IAHC"
    hash = append_u64(hash, previous_chain_hash);
    hash = append_u32(hash, command_sequence);
    hash = append_u64(hash, command_hash);
    hash = append_u64(hash, collision_hash);
    return append_u64(hash, state_hash);
}

extern "C" bool Worr_PredictionReplayCountV1(
    uint32_t acknowledged_sequence, uint32_t current_sequence,
    uint32_t capacity, uint32_t *replay_count)
{
    if (!replay_count || capacity == 0)
        return false;

    const uint32_t count = current_sequence - acknowledged_sequence;
    if (count >= capacity)
        return false;
    *replay_count = count;
    return true;
}

extern "C" bool Worr_PredictionReplaySequenceV1(
    uint32_t acknowledged_sequence, uint32_t replay_count,
    uint32_t replay_index, uint32_t *command_sequence)
{
    if (!command_sequence || replay_index >= replay_count)
        return false;
    *command_sequence = acknowledged_sequence + replay_index + 1u;
    return true;
}
