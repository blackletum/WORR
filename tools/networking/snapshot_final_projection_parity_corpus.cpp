/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

/*
 * Deterministic offline FR-10-T06 final-emission/projector parity corpus.
 *
 * This harness deliberately uses only the public server shadow and canonical
 * q2proto projection APIs.  It is not a live packet, engine, or cgame test.
 */

#include "server/snapshot_shadow.h"

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <limits>
#include <string_view>
#include <vector>

namespace {

constexpr uint32_t max_entities = 64;
constexpr uint32_t max_models = 64;
constexpr uint32_t max_sounds = 64;
constexpr uint32_t entities_per_slot = 48;
constexpr uint32_t area_bytes_per_slot = 16;
constexpr uint32_t controlled_entity = 1;
constexpr uint32_t baseline_entity = 55;
constexpr uint32_t reuse_entity = 59;
constexpr uint32_t visibility_entity = 60;

[[noreturn]] void fail(const char *expression, int line)
{
    std::fprintf(stderr,
                 "snapshot_final_projection_parity_corpus:%d: %s\n",
                 line, expression);
    std::exit(EXIT_FAILURE);
}

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression))                                                    \
            fail(#expression, __LINE__);                                      \
    } while (0)

uint32_t parse_u32(const char *text)
{
    if (!text || !*text)
        fail("non-empty uint32 argument", __LINE__);
    char *end = nullptr;
    const unsigned long long value = std::strtoull(text, &end, 0);
    if (!end || *end != '\0' || value > UINT32_MAX)
        fail("valid uint32 argument", __LINE__);
    return static_cast<uint32_t>(value);
}

struct options_t {
    uint32_t snapshots = 100000;
    uint32_t seed = UINT32_C(0x5a17c9e3);
    uint32_t retention_slots = 64;
    uint32_t boundary_cases = 8;
};

options_t parse_options(int argc, char **argv)
{
    options_t result;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);
        CHECK(i + 1 < argc);
        if (argument == "--snapshots")
            result.snapshots = parse_u32(argv[++i]);
        else if (argument == "--seed")
            result.seed = parse_u32(argv[++i]);
        else if (argument == "--retention-slots")
            result.retention_slots = parse_u32(argv[++i]);
        else if (argument == "--boundary-cases")
            result.boundary_cases = parse_u32(argv[++i]);
        else
            fail("known command-line argument", __LINE__);
    }
    CHECK(result.snapshots >= 1000);
    CHECK(result.boundary_cases >= 2);
    CHECK(result.boundary_cases < result.snapshots);
    CHECK(result.retention_slots >= 32);
    CHECK(result.retention_slots <= 256);
    return result;
}

class rng_t {
  public:
    explicit rng_t(uint32_t seed) : state_(seed ? seed : 1u) {}

    uint32_t next()
    {
        uint32_t value = state_;
        value ^= value << 13;
        value ^= value >> 17;
        value ^= value << 5;
        state_ = value;
        return value;
    }

  private:
    uint32_t state_;
};

struct entity_model_t {
    bool present = false;
    std::array<float, 3> origin{};
    uint16_t model = 0;
    uint16_t frame = 0;
    uint32_t skin = 0;
    uint32_t effects = 0;
    uint32_t renderfx = 0;
    uint16_t sound = 0;
    uint32_t solid = 0;
    uint8_t alpha = 255;
    uint8_t scale = 16;
};

bool same_model(const entity_model_t &left, const entity_model_t &right)
{
    return left.present == right.present && left.origin == right.origin &&
           left.model == right.model &&
           left.frame == right.frame && left.skin == right.skin &&
           left.effects == right.effects &&
           left.renderfx == right.renderfx && left.sound == right.sound &&
           left.solid == right.solid && left.alpha == right.alpha &&
           left.scale == right.scale;
}

using entity_state_t = std::array<entity_model_t, max_entities>;

struct retained_state_t {
    int32_t wire_frame = -1;
    entity_state_t entities{};
};

struct frame_carrier_t {
    q2proto_svc_frame_t frame{};
    std::array<uint8_t, 8> area{};
    std::vector<q2proto_svc_frame_entity_delta_t> deltas;
};

void decoded_coords_to_server_write(q2proto_maybe_diff_coords_t &coords)
{
    q2proto_vec3_t current;
    q2proto_var_coords_get_float(&coords.read.value.values, current);
    std::memset(&coords, 0, sizeof(coords));
    q2proto_var_coords_set_float(&coords.write.prev, current);
    q2proto_var_coords_set_float(&coords.write.current, current);
}

q2proto_entity_state_delta_t server_write_delta(
    const q2proto_entity_state_delta_t &decoded)
{
    auto result = decoded;
    decoded_coords_to_server_write(result.origin);
    return result;
}

frame_carrier_t server_write_carrier(const frame_carrier_t &decoded)
{
    auto result = decoded;
    result.frame.areabits = result.area.data();
    decoded_coords_to_server_write(result.frame.playerstate.pm_origin);
    decoded_coords_to_server_write(result.frame.playerstate.pm_velocity);
    for (auto &delta : result.deltas) {
        delta.entity_delta = server_write_delta(delta.entity_delta);
    }
    return result;
}

q2proto_entity_state_delta_t full_entity_delta(
    const entity_model_t &model, uint8_t event)
{
    q2proto_entity_state_delta_t result{};
    result.delta_bits = Q2P_ESD_MODELINDEX | Q2P_ESD_FRAME |
                        Q2P_ESD_SKINNUM | Q2P_ESD_EFFECTS |
                        Q2P_ESD_RENDERFX | Q2P_ESD_SOUND |
                        Q2P_ESD_SOLID | Q2P_ESD_ALPHA | Q2P_ESD_SCALE;
    result.modelindex = model.model;
    result.frame = model.frame;
    result.skinnum = model.skin;
    result.effects = model.effects;
    result.renderfx = model.renderfx;
    result.sound = model.sound;
    result.solid = model.solid;
    result.alpha = model.alpha;
    result.scale = model.scale;
    result.origin.read.diff_bits = 0;
    result.origin.read.value.delta_bits = 7;
    q2proto_var_coords_set_float(&result.origin.read.value.values,
                                 model.origin.data());
    if (event != 0) {
        result.delta_bits |= Q2P_ESD_EVENT;
        result.event = event;
    }
    return result;
}

q2proto_entity_state_delta_t baseline_delta()
{
    entity_model_t model;
    model.present = true;
    model.model = 7;
    model.frame = 1;
    model.skin = 3;
    model.effects = 2;
    model.renderfx = 4;
    model.sound = 5;
    model.solid = 31;
    return full_entity_delta(model, 0);
}

void append_entity(frame_carrier_t &carrier, uint32_t entity_index,
                   const q2proto_entity_state_delta_t &delta,
                   bool remove = false)
{
    q2proto_svc_frame_entity_delta_t item{};
    item.newnum = static_cast<uint16_t>(entity_index);
    item.remove = remove;
    item.entity_delta = delta;
    carrier.deltas.push_back(item);
}

q2proto_svc_playerstate_t player_delta(bool keyframe, uint32_t ordinal)
{
    q2proto_svc_playerstate_t result{};
    if (keyframe) {
        result.delta_bits = Q2P_PSD_PM_GRAVITY | Q2P_PSD_FOV |
                            Q2P_PSD_PM_VIEWHEIGHT;
        result.pm_gravity = 800;
        result.pm_viewheight = 22;
        result.fov = static_cast<uint8_t>(90u + (ordinal % 11u));
        result.statbits = UINT64_C(1) << 2;
        result.stats[2] = static_cast<int16_t>(ordinal % 1000u);
    } else if ((ordinal % 37u) == 0) {
        result.delta_bits = Q2P_PSD_FOV;
        result.fov = static_cast<uint8_t>(90u + (ordinal % 11u));
        result.statbits = UINT64_C(1) << 2;
        result.stats[2] = static_cast<int16_t>(ordinal % 1000u);
    }
    return result;
}

frame_carrier_t make_carrier(
    int32_t wire_frame, int32_t wire_base, uint32_t ordinal,
    uint32_t tick_delta, bool keyframe, const entity_state_t &base,
    const entity_state_t &target, bool emit_event, bool baseline_add)
{
    frame_carrier_t result;
    result.frame.serverframe = wire_frame;
    result.frame.deltaframe = wire_base;
    result.frame.suppress_count = static_cast<uint8_t>(
        tick_delta > 1 ? std::min<uint32_t>(tick_delta - 1u, UINT8_MAX) : 0);
    result.area = {
        static_cast<uint8_t>(ordinal),
        static_cast<uint8_t>(ordinal >> 8),
        static_cast<uint8_t>(ordinal >> 16),
        static_cast<uint8_t>(wire_frame),
        static_cast<uint8_t>(wire_frame >> 8),
        static_cast<uint8_t>(tick_delta),
        static_cast<uint8_t>(ordinal ^ UINT32_C(0xa5)),
        static_cast<uint8_t>((ordinal * 17u) >> 3),
    };
    result.frame.areabits_len = static_cast<uint8_t>(result.area.size());
    result.frame.areabits = result.area.data();
    result.frame.playerstate = player_delta(keyframe, ordinal);

    const uint32_t event_entity = 2;
    for (uint32_t index = 1; index < max_entities; ++index) {
        const bool from_present = !keyframe && base[index].present;
        const bool to_present = target[index].present;
        const bool event_here = emit_event && index == event_entity;
        if (from_present && !to_present) {
            append_entity(result, index, {}, true);
        } else if (to_present &&
                   (!from_present || !same_model(base[index], target[index]) ||
                    event_here)) {
            if (baseline_add && index == baseline_entity && !from_present) {
                q2proto_entity_state_delta_t delta{};
                delta.delta_bits = Q2P_ESD_FRAME;
                delta.frame = target[index].frame;
                append_entity(result, index, delta);
            } else {
                append_entity(
                    result, index,
                    full_entity_delta(
                        target[index],
                        event_here ? WORR_EVENT_LEGACY_ENTITY_FOOTSTEP : 0));
            }
        }
    }
    result.deltas.push_back({});
    return result;
}

entity_model_t generated_model(uint32_t entity_index, uint32_t ordinal,
                               rng_t &rng)
{
    entity_model_t result;
    result.present = true;
    result.origin = {
        static_cast<float>((ordinal + entity_index * 3u) & 1023u),
        static_cast<float>((ordinal * 2u + entity_index * 5u) & 1023u),
        static_cast<float>((ordinal * 3u + entity_index * 7u) & 511u),
    };
    result.model = static_cast<uint16_t>(1u +
        ((entity_index * 7u + ordinal) % (max_models - 1u)));
    result.frame = static_cast<uint16_t>(ordinal ^ (entity_index * 131u));
    result.skin = (ordinal * 3u + entity_index) & 31u;
    result.effects = (rng.next() & 15u);
    result.renderfx = (rng.next() & ((UINT32_C(1) << 15) - 1u));
    result.sound = static_cast<uint16_t>((ordinal + entity_index) % max_sounds);
    result.solid = (ordinal * 17u + entity_index * 19u) & 0xffffu;
    result.alpha = static_cast<uint8_t>(128u + (ordinal % 128u));
    result.scale = static_cast<uint8_t>(8u + (entity_index % 17u));
    return result;
}

struct projector_t {
    worr_snapshot_q2proto_context_v2 context{};
    std::vector<worr_snapshot_q2proto_slot_v2> slots;
    std::vector<worr_snapshot_entity_v2> entities;
    std::vector<uint8_t> areas;
    std::vector<worr_snapshot_event_ref_v2> events;
    std::vector<worr_snapshot_q2proto_lineage_v2> lineages;
    std::array<worr_snapshot_entity_v2, max_entities> baselines{};
    std::array<uint8_t, max_entities> baseline_present{};
    std::array<worr_snapshot_entity_v2, entities_per_slot> scratch_entities{};
    std::array<uint8_t, area_bytes_per_slot> scratch_area{};
    std::array<worr_snapshot_event_ref_v2, entities_per_slot> scratch_events{};
    std::array<worr_snapshot_q2proto_lineage_v2, max_entities> scratch_lineage{};

    projector_t(uint32_t retention_slots, uint32_t epoch)
        : slots(retention_slots),
          entities(static_cast<size_t>(retention_slots) * entities_per_slot),
          areas(static_cast<size_t>(retention_slots) * area_bytes_per_slot),
          events(static_cast<size_t>(retention_slots) * entities_per_slot),
          lineages(static_cast<size_t>(retention_slots) * max_entities)
    {
        worr_snapshot_q2proto_profile_v2 profile{};
        profile.struct_size = sizeof(profile);
        profile.schema_version = WORR_SNAPSHOT_Q2PROTO_VERSION;
        profile.snapshot_epoch = epoch;
        profile.max_entities = max_entities;
        profile.max_models = max_models;
        profile.max_sounds = max_sounds;
        profile.beam_renderfx_mask = UINT32_C(1) << 7;
        profile.legacy_renderfx_allowed_mask =
            (UINT32_C(1) << 19) - 1u;
        profile.legacy_beam_clear_mask = UINT32_C(1) << 9;
        profile.extended_entity_state = 1;

        worr_snapshot_q2proto_storage_v2 storage{};
        storage.struct_size = sizeof(storage);
        storage.schema_version = WORR_SNAPSHOT_Q2PROTO_VERSION;
        storage.slots = slots.data();
        storage.entities = entities.data();
        storage.area_bytes = areas.data();
        storage.event_refs = events.data();
        storage.lineages = lineages.data();
        storage.baselines = baselines.data();
        storage.baseline_present = baseline_present.data();
        storage.scratch_entities = scratch_entities.data();
        storage.scratch_area_bytes = scratch_area.data();
        storage.scratch_event_refs = scratch_events.data();
        storage.scratch_lineage = scratch_lineage.data();
        storage.slot_capacity = retention_slots;
        storage.entities_per_slot = entities_per_slot;
        storage.area_bytes_per_slot = area_bytes_per_slot;
        storage.event_refs_per_slot = entities_per_slot;
        storage.entity_storage_capacity =
            static_cast<uint32_t>(entities.size());
        storage.area_storage_capacity = static_cast<uint32_t>(areas.size());
        storage.event_storage_capacity = static_cast<uint32_t>(events.size());
        storage.lineage_storage_capacity =
            static_cast<uint32_t>(lineages.size());
        storage.scratch_entity_capacity = scratch_entities.size();
        storage.scratch_area_capacity = scratch_area.size();
        storage.scratch_event_capacity = scratch_events.size();
        storage.scratch_lineage_capacity = scratch_lineage.size();
        CHECK(Worr_SnapshotQ2ProtoInitV2(&context, &profile, &storage) ==
              WORR_SNAPSHOT_Q2PROTO_OK);
    }
};

sv_snapshot_shadow_config_v1 server_config(uint32_t retention_slots,
                                           uint32_t epoch)
{
    sv_snapshot_shadow_config_v1 result{};
    result.struct_size = sizeof(result);
    result.schema_version = SV_SNAPSHOT_SHADOW_VERSION;
    result.snapshot_epoch = epoch;
    result.max_entities = max_entities;
    result.max_models = max_models;
    result.max_sounds = max_sounds;
    result.slot_capacity = retention_slots;
    result.entities_per_slot = entities_per_slot;
    result.area_bytes_per_slot = area_bytes_per_slot;
    result.beam_renderfx_mask = UINT32_C(1) << 7;
    result.legacy_renderfx_allowed_mask = (UINT32_C(1) << 19) - 1u;
    result.legacy_beam_clear_mask = UINT32_C(1) << 9;
    result.extended_entity_state = 1;
    return result;
}

struct coverage_t {
    uint64_t snapshots = 0;
    uint64_t keyframes = 0;
    uint64_t zero_base_keyframes = 0;
    uint64_t acknowledged_branches = 0;
    uint64_t invalid_base_rejections = 0;
    uint64_t invalid_base_keyframe_recoveries = 0;
    uint64_t entity_adds = 0;
    uint64_t entity_updates = 0;
    uint64_t entity_removes = 0;
    uint64_t entity_reuses = 0;
    uint64_t visibility_gap_snapshots = 0;
    uint64_t visibility_gap_reentries = 0;
    uint64_t truncations = 0;
    uint64_t fragment_stalls = 0;
    uint64_t rate_suppressions = 0;
    uint64_t event_snapshots = 0;
    uint64_t endpoint_hash_matches = 0;
    uint64_t legacy_hash_matches = 0;
    uint64_t component_hash_matches = 0;
    uint64_t unchanged_old_origin_matches = 0;
    uint64_t exact_chronology_matches = 0;
    uint64_t authoritative_tick_wraps = 0;
    uint64_t wire_sequence_boundary_cases = 0;
    uint64_t wire_sequence_wrap_cases = 0;
    uint64_t stale_ref_rejections = 0;
    uint64_t digest = UINT64_C(1469598103934665603);
};

void digest_u64(coverage_t &coverage, uint64_t value)
{
    for (unsigned i = 0; i < 8; ++i) {
        coverage.digest ^= static_cast<uint8_t>(value >> (i * 8u));
        coverage.digest *= UINT64_C(1099511628211);
    }
}

struct published_view_t {
    worr_snapshot_ref_v2 ref{};
    worr_snapshot_projection_view_v2 view{};
    worr_snapshot_projection_hashes_v2 hashes{};
};

worr_snapshot_q2proto_frame_input_v2 receiver_input(
    const frame_carrier_t &carrier, uint64_t server_time_us,
    int32_t lineage_parent, uint32_t movement_type,
    uint16_t movement_flags, uint8_t team_id, uint32_t input_flags,
    uint32_t ordinal)
{
    worr_snapshot_q2proto_frame_input_v2 result{};
    result.struct_size = sizeof(result);
    result.schema_version = WORR_SNAPSHOT_Q2PROTO_VERSION;
    result.frame = &carrier.frame;
    result.entity_deltas = carrier.deltas.data();
    result.entity_delta_count = static_cast<uint32_t>(carrier.deltas.size());
    result.flags = input_flags |
                   WORR_SNAPSHOT_Q2PROTO_FRAME_MOVEMENT_TYPE_VALID |
                   WORR_SNAPSHOT_Q2PROTO_FRAME_MOVEMENT_FLAGS_VALID |
                   WORR_SNAPSHOT_Q2PROTO_FRAME_TEAM_ID_VALID;
    result.controlled_entity_index = controlled_entity;
    result.canonical_movement_type = static_cast<int32_t>(movement_type);
    result.canonical_movement_flags = movement_flags;
    result.team_id = team_id;
    if ((input_flags &
         WORR_SNAPSHOT_Q2PROTO_FRAME_LINEAGE_PARENT_VALID) != 0) {
        result.lineage_parent_serverframe = lineage_parent;
    }
    result.server_time_us = server_time_us;
    if ((ordinal % 3u) != 0) {
        result.consumed_command.cursor.epoch = 23;
        result.consumed_command.cursor.contiguous_sequence = ordinal + 1u;
        result.consumed_command.provenance =
            WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED;
    }
    return result;
}

sv_snapshot_shadow_ref_v1 publish_server(
    sv_snapshot_shadow_peer_v1 *peer, const frame_carrier_t &carrier,
    uint32_t server_tick, uint32_t tick_delta, uint64_t server_time_us,
    uint32_t movement_type, uint16_t movement_flags, uint8_t team_id,
    uint32_t ordinal, bool truncated, bool fragment_stall)
{
    /* q2proto exposes direction-specific union members: the independent
     * receiver consumes decoded read coordinates, while the production server
     * adapter observes write.previous/current coordinates before emission. */
    auto server_carrier = server_write_carrier(carrier);
    sv_snapshot_shadow_frame_v1 input{};
    input.struct_size = sizeof(input);
    input.schema_version = SV_SNAPSHOT_SHADOW_VERSION;
    input.wire_frame = &server_carrier.frame;
    input.authoritative_server_tick = server_tick;
    input.authoritative_tick_delta = tick_delta;
    input.authoritative_server_time_us = server_time_us;
    input.controlled_entity_index = controlled_entity;
    input.canonical_movement_type = static_cast<int32_t>(movement_type);
    input.canonical_movement_flags = movement_flags;
    input.team_id = team_id;
    if ((ordinal % 3u) != 0) {
        input.consumed_command.cursor.epoch = 23;
        input.consumed_command.cursor.contiguous_sequence = ordinal + 1u;
        input.consumed_command.provenance =
            WORR_SNAPSHOT_CONSUMED_COMMAND_SERVER_CONSUMED;
    }
    CHECK(SV_SnapshotShadowBeginFrameV1(peer, &input) ==
          SV_SNAPSHOT_SHADOW_OK);
    for (const auto &delta : server_carrier.deltas) {
        CHECK(SV_SnapshotShadowCaptureEntityDeltaV1(peer, &delta) ==
              SV_SNAPSHOT_SHADOW_OK);
    }
    if (truncated)
        SV_SnapshotShadowMarkTransportTruncatedV1(peer);
    if (fragment_stall)
        SV_SnapshotShadowMarkFragmentStallV1(peer);
    sv_snapshot_shadow_ref_v1 ref{};
    CHECK(SV_SnapshotShadowCommitFrameV1(peer, &ref) ==
          SV_SNAPSHOT_SHADOW_OK);
    return ref;
}

published_view_t publish_receiver(
    projector_t &projector, const frame_carrier_t &carrier,
    uint64_t server_time_us, int32_t lineage_parent,
    uint32_t movement_type, uint16_t movement_flags, uint8_t team_id,
    uint32_t ordinal, uint32_t flags)
{
    const auto input = receiver_input(
        carrier, server_time_us, lineage_parent, movement_type,
        movement_flags, team_id, flags, ordinal);
    published_view_t result;
    const auto publish_result = Worr_SnapshotQ2ProtoPublishV2(
        &projector.context, &input, &result.ref);
    if (publish_result != WORR_SNAPSHOT_Q2PROTO_OK) {
        std::fprintf(
            stderr,
            "receiver publish failed: result=%u ordinal=%u wire=%d base=%d "
            "flags=0x%x deltas=%u\n",
            static_cast<unsigned>(publish_result), ordinal,
            carrier.frame.serverframe, carrier.frame.deltaframe, flags,
            input.entity_delta_count);
        fail("receiver projector publication", __LINE__);
    }
    CHECK(Worr_SnapshotQ2ProtoViewV2(
              &projector.context, result.ref, &result.view,
              &result.hashes) == WORR_SNAPSHOT_Q2PROTO_OK);
    return result;
}

void count_entity_changes(const entity_state_t &base,
                          const entity_state_t &target,
                          coverage_t &coverage)
{
    for (uint32_t index = 1; index < max_entities; ++index) {
        if (!base[index].present && target[index].present) {
            ++coverage.entity_adds;
            if (index == reuse_entity)
                ++coverage.entity_reuses;
        } else if (base[index].present && !target[index].present) {
            ++coverage.entity_removes;
        } else if (base[index].present && target[index].present &&
                   !same_model(base[index], target[index])) {
            ++coverage.entity_updates;
        }
    }
}

const worr_snapshot_entity_v2 *find_entity(
    const worr_snapshot_projection_view_v2 &view, uint32_t entity_index)
{
    for (uint32_t i = 0; i < view.entity_count; ++i) {
        if (view.entities[i].generation.identity.index == entity_index)
            return &view.entities[i];
    }
    return nullptr;
}

void verify_unchanged_old_origins(
    const worr_snapshot_projection_view_v2 &view,
    const entity_state_t &base, const entity_state_t &target,
    bool keyframe, coverage_t &coverage)
{
    if (keyframe)
        return;
    for (uint32_t index = 1; index < max_entities; ++index) {
        if (!base[index].present || !target[index].present ||
            !same_model(base[index], target[index])) {
            continue;
        }
        const auto *entity = find_entity(view, index);
        CHECK(entity != nullptr);
        if ((entity->renderfx & (UINT32_C(1) << 7)) != 0)
            continue;
        CHECK(std::memcmp(entity->old_origin, entity->origin,
                          sizeof(entity->origin)) == 0);
        ++coverage.unchanged_old_origin_matches;
    }
}

void verify_parity(
    sv_snapshot_shadow_peer_v1 *peer, sv_snapshot_shadow_ref_v1 server_ref,
    const published_view_t &receiver, uint32_t wire_frame,
    int32_t wire_base, uint32_t server_tick, uint32_t tick_delta,
    uint64_t server_time_us, bool keyframe, bool truncated,
    bool fragment_stall, bool rate_suppressed, coverage_t &coverage)
{
    sv_snapshot_shadow_sent_v1 sent{};
    CHECK(SV_SnapshotShadowGetSentV1(peer, server_ref, &sent) ==
          SV_SNAPSHOT_SHADOW_OK);
    worr_snapshot_projection_view_v2 server_view{};
    worr_snapshot_projection_hashes_v2 server_hashes{};
    CHECK(SV_SnapshotShadowViewV1(
              peer, server_ref, &server_view, &server_hashes) ==
          SV_SNAPSHOT_SHADOW_OK);

    CHECK(sent.wire_snapshot_number == wire_frame);
    CHECK(sent.wire_base_snapshot_number == wire_base);
    CHECK(sent.authoritative_server_tick == server_tick);
    CHECK(sent.authoritative_tick_delta == tick_delta);
    CHECK(server_view.snapshot->server_tick == server_tick);
    CHECK(server_view.snapshot->server_time_us == server_time_us);
    CHECK(server_view.snapshot->discontinuity.server_tick_delta == tick_delta);
    CHECK(server_view.entity_count == receiver.view.entity_count);
    CHECK(server_view.area_byte_count == receiver.view.area_byte_count);
    CHECK(server_view.event_ref_count == receiver.view.event_ref_count);

    CHECK(server_hashes.legacy_parity_hash ==
          receiver.hashes.legacy_parity_hash);
    ++coverage.legacy_hash_matches;
    CHECK(server_hashes.semantic_player_hash ==
          receiver.hashes.semantic_player_hash);
    CHECK(server_hashes.semantic_entity_hash ==
          receiver.hashes.semantic_entity_hash);
    CHECK(server_hashes.semantic_area_hash ==
          receiver.hashes.semantic_area_hash);
    CHECK(server_hashes.semantic_event_hash ==
          receiver.hashes.semantic_event_hash);
    ++coverage.component_hash_matches;

    worr_snapshot_v2 normalized = *receiver.view.snapshot;
    normalized.server_tick = server_tick;
    normalized.server_time_us = server_time_us;
    normalized.discontinuity.server_tick_delta = tick_delta;
    CHECK(Worr_SnapshotCalculateHashV2(
        &normalized, max_entities, &normalized.snapshot_hash));
    worr_snapshot_projection_view_v2 normalized_view = receiver.view;
    normalized_view.snapshot = &normalized;
    worr_snapshot_projection_hashes_v2 normalized_hashes{};
    CHECK(Worr_SnapshotProjectionHashesV2(
        &normalized_view, max_entities, &normalized_hashes));
    CHECK(server_hashes.endpoint_hash == normalized_hashes.endpoint_hash);
    ++coverage.endpoint_hash_matches;

    CHECK(((server_view.snapshot->flags & WORR_SNAPSHOT_FLAG_KEYFRAME) != 0) ==
          keyframe);
    CHECK(((server_view.snapshot->flags &
            WORR_SNAPSHOT_FLAG_TRANSPORT_TRUNCATED) != 0) == truncated);
    CHECK(((server_view.snapshot->discontinuity.flags &
            WORR_SNAPSHOT_DISCONTINUITY_FRAGMENT_STALL) != 0) ==
          fragment_stall);
    CHECK(((server_view.snapshot->discontinuity.flags &
            WORR_SNAPSHOT_DISCONTINUITY_RATE_SUPPRESSED) != 0) ==
          rate_suppressed);
    ++coverage.exact_chronology_matches;

    digest_u64(coverage, server_hashes.endpoint_hash);
    digest_u64(coverage, server_hashes.legacy_parity_hash);
    digest_u64(coverage, server_tick);
    digest_u64(coverage, server_time_us);
}

void reject_overwritten_base(
    sv_snapshot_shadow_peer_v1 *peer, projector_t &receiver,
    int32_t current_wire, int32_t overwritten_base, uint32_t server_tick,
    uint64_t server_time_us, uint32_t ordinal, coverage_t &coverage)
{
    entity_state_t empty{};
    auto carrier = make_carrier(
        current_wire, overwritten_base, ordinal, 1, false, empty, empty,
        false, false);
    carrier.frame.areabits = carrier.area.data();
    auto server_carrier = server_write_carrier(carrier);
    sv_snapshot_shadow_frame_v1 server_input{};
    server_input.struct_size = sizeof(server_input);
    server_input.schema_version = SV_SNAPSHOT_SHADOW_VERSION;
    server_input.wire_frame = &server_carrier.frame;
    server_input.authoritative_server_tick = server_tick;
    server_input.authoritative_tick_delta = 1;
    server_input.authoritative_server_time_us = server_time_us;
    server_input.controlled_entity_index = controlled_entity;
    CHECK(SV_SnapshotShadowBeginFrameV1(peer, &server_input) ==
          SV_SNAPSHOT_SHADOW_OK);
    for (const auto &delta : server_carrier.deltas) {
        CHECK(SV_SnapshotShadowCaptureEntityDeltaV1(peer, &delta) ==
              SV_SNAPSHOT_SHADOW_OK);
    }
    sv_snapshot_shadow_ref_v1 untouched{77, 88};
    CHECK(SV_SnapshotShadowCommitFrameV1(peer, &untouched) ==
          SV_SNAPSHOT_SHADOW_BASE_REF_MISSING);
    CHECK(untouched.slot == 77 && untouched.generation == 88);

    const auto input = receiver_input(
        carrier, server_time_us, -1, 0, 0, 0, 0, ordinal);
    worr_snapshot_ref_v2 receiver_ref{77, 88};
    CHECK(Worr_SnapshotQ2ProtoPublishV2(
              &receiver.context, &input, &receiver_ref) ==
          WORR_SNAPSHOT_Q2PROTO_INVALID_BASE);
    CHECK(receiver_ref.slot == 77 && receiver_ref.generation == 88);
    ++coverage.invalid_base_rejections;
}

void run_session(uint32_t count, int32_t first_wire, uint32_t epoch,
                 uint32_t ordinal_base, const options_t &options, rng_t &rng,
                 coverage_t &coverage, bool boundary_session)
{
    const auto config = server_config(options.retention_slots, epoch);
    sv_snapshot_shadow_peer_v1 *peer = SV_SnapshotShadowCreateV1(&config);
    CHECK(peer != nullptr);
    projector_t receiver(options.retention_slots, epoch);
    const auto baseline = baseline_delta();
    const auto server_baseline = server_write_delta(baseline);
    CHECK(SV_SnapshotShadowSetBaselineV1(
              peer, baseline_entity, &server_baseline) ==
          SV_SNAPSHOT_SHADOW_OK);
    CHECK(Worr_SnapshotQ2ProtoSetBaselineV2(
              &receiver.context, baseline_entity, &baseline) ==
          WORR_SNAPSHOT_Q2PROTO_OK);

    std::deque<retained_state_t> history;
    uint32_t server_tick = boundary_session ? 1000u : UINT32_C(0xffff0000);
    uint64_t server_time_us = boundary_session
        ? UINT64_C(9000000000)
        : UINT64_C(1000000);
    bool recovering_invalid_base = false;

    for (uint32_t local = 0; local < count; ++local) {
        const uint32_t ordinal = ordinal_base + local;
        const int64_t wire64 = static_cast<int64_t>(first_wire) + local;
        CHECK(wire64 >= 0 && wire64 <= INT32_MAX);
        const int32_t wire_frame = static_cast<int32_t>(wire64);

        bool keyframe = history.empty() || (ordinal % 997u) == 0;
        if (!history.empty() && (ordinal % 4093u) == 0 &&
            history.size() == options.retention_slots) {
            const int32_t overwritten = history.front().wire_frame - 1;
            if (overwritten > 0) {
                reject_overwritten_base(
                    peer, receiver, wire_frame, overwritten, server_tick,
                    server_time_us, ordinal, coverage);
                keyframe = true;
                recovering_invalid_base = true;
            }
        }

        const retained_state_t *base_record = nullptr;
        int32_t wire_base = -1;
        if (!keyframe) {
            const uint32_t max_distance = std::min<uint32_t>(
                16u, static_cast<uint32_t>(history.size()));
            uint32_t distance = 1u + (rng.next() % max_distance);
            base_record = &history[history.size() - distance];
            if (base_record->wire_frame <= 0) {
                base_record = &history.back();
                if (base_record->wire_frame <= 0)
                    keyframe = true;
            }
            if (!keyframe) {
                wire_base = base_record->wire_frame;
                if (base_record != &history.back())
                    ++coverage.acknowledged_branches;
            }
        }
        if (keyframe) {
            base_record = history.empty() ? nullptr : &history.back();
            if (!history.empty() && (ordinal & 1u) == 0) {
                wire_base = 0;
                ++coverage.zero_base_keyframes;
            }
            ++coverage.keyframes;
        }

        entity_state_t base{};
        if (base_record)
            base = base_record->entities;
        entity_state_t target = base;
        if (history.empty() && !boundary_session) {
            for (uint32_t index : {2u, 3u, 4u, baseline_entity})
                target[index] = generated_model(index, ordinal + index, rng);
            target[baseline_entity].model = 7;
            target[baseline_entity].frame = 9;
            target[baseline_entity].skin = 3;
            target[baseline_entity].effects = 2;
            target[baseline_entity].renderfx = 4;
            target[baseline_entity].sound = 5;
            target[baseline_entity].solid = 31;
        }

        const uint32_t mutation_entity = 2u + (rng.next() % 44u);
        if (target[mutation_entity].present && (ordinal % 7u) == 0) {
            target[mutation_entity] = {};
        } else {
            target[mutation_entity] =
                generated_model(mutation_entity, ordinal, rng);
        }

        const bool reuse_visible = ((ordinal / 11u) & 1u) == 0;
        if (reuse_visible) {
            if (!target[reuse_entity].present) {
                target[reuse_entity] =
                    generated_model(reuse_entity, ordinal, rng);
            }
        } else {
            target[reuse_entity] = {};
        }

        const bool pvs_visible = (ordinal % 80u) < 20u;
        if (pvs_visible) {
            if (!target[visibility_entity].present) {
                target[visibility_entity] =
                    generated_model(visibility_entity, ordinal, rng);
                ++coverage.visibility_gap_reentries;
            }
        } else {
            target[visibility_entity] = {};
            ++coverage.visibility_gap_snapshots;
        }

        const bool event_snapshot = (ordinal % 97u) == 0;
        if (event_snapshot && !target[2].present)
            target[2] = generated_model(2, ordinal, rng);
        const bool baseline_add = history.empty() && !boundary_session;
        const uint32_t tick_delta = history.empty()
            ? 0u
            : ((ordinal % 43u) == 0 ? 3u
                                    : ((ordinal % 19u) == 0 ? 2u : 1u));
        if (!history.empty()) {
            const uint32_t previous_tick = server_tick;
            server_tick += tick_delta;
            if (server_tick < previous_tick)
                ++coverage.authoritative_tick_wraps;
            const uint64_t interval_us =
                ((ordinal / 1234u) & 1u) != 0
                    ? UINT64_C(16666)
                    : UINT64_C(25000);
            CHECK(tick_delta <=
                  (UINT64_MAX - server_time_us) / interval_us);
            server_time_us += static_cast<uint64_t>(tick_delta) * interval_us;
        }

        const bool truncated = (ordinal % 401u) == 0;
        const bool fragment_stall = (ordinal % 613u) == 0;
        const bool rate_suppressed = tick_delta > 1;
        const uint32_t movement_type = ordinal % 5u;
        const uint16_t movement_flags =
            static_cast<uint16_t>((ordinal * 3u) & 0x3ffu);
        const uint8_t team_id = static_cast<uint8_t>(ordinal % 4u);

        auto carrier = make_carrier(
            wire_frame, wire_base, ordinal, tick_delta, keyframe, base,
            target, event_snapshot, baseline_add);
        carrier.frame.areabits = carrier.area.data();
        uint32_t receiver_flags = 0;
        int32_t lineage_parent = -1;
        if (history.empty() && wire_frame > 0) {
            receiver_flags |= WORR_SNAPSHOT_Q2PROTO_FRAME_OBSERVER_ATTACH;
        } else if (!history.empty() && keyframe) {
            receiver_flags |=
                WORR_SNAPSHOT_Q2PROTO_FRAME_LINEAGE_PARENT_VALID;
            lineage_parent = history.back().wire_frame;
        }
        if (truncated) {
            receiver_flags |=
                WORR_SNAPSHOT_Q2PROTO_FRAME_TRANSPORT_TRUNCATED;
            ++coverage.truncations;
        }
        if (fragment_stall) {
            receiver_flags |= WORR_SNAPSHOT_Q2PROTO_FRAME_FRAGMENT_STALL;
            ++coverage.fragment_stalls;
        }
        if (rate_suppressed)
            ++coverage.rate_suppressions;
        if (event_snapshot)
            ++coverage.event_snapshots;

        const auto server_ref = publish_server(
            peer, carrier, server_tick, tick_delta, server_time_us,
            movement_type, movement_flags, team_id, ordinal, truncated,
            fragment_stall);
        const auto receiver_view = publish_receiver(
            receiver, carrier, server_time_us, lineage_parent,
            movement_type, movement_flags, team_id, ordinal, receiver_flags);
        verify_parity(
            peer, server_ref, receiver_view,
            static_cast<uint32_t>(wire_frame), wire_base, server_tick,
            tick_delta, server_time_us, keyframe, truncated, fragment_stall,
            rate_suppressed, coverage);
        verify_unchanged_old_origins(
            receiver_view.view, base, target, keyframe, coverage);

        count_entity_changes(base, target, coverage);
        if (recovering_invalid_base) {
            ++coverage.invalid_base_keyframe_recoveries;
            recovering_invalid_base = false;
        }
        if (boundary_session)
            ++coverage.wire_sequence_boundary_cases;

        retained_state_t retained;
        retained.wire_frame = wire_frame;
        retained.entities = target;
        history.push_back(retained);
        if (history.size() > options.retention_slots) {
            const auto stale_wire = history.front().wire_frame;
            history.pop_front();
            sv_snapshot_shadow_ref_v1 stale_ref{};
            CHECK(SV_SnapshotShadowFindWireV1(
                      peer, stale_wire, &stale_ref) ==
                  SV_SNAPSHOT_SHADOW_STALE_REF);
            ++coverage.stale_ref_rejections;
        }
        ++coverage.snapshots;
    }

    sv_snapshot_shadow_status_v1 status{};
    CHECK(SV_SnapshotShadowGetStatusV1(peer, &status));
    CHECK(status.frames_committed == count);
    CHECK(status.last_endpoint_hash != 0);
    CHECK(status.last_legacy_parity_hash != 0);
    SV_SnapshotShadowDestroyV1(peer);
}

void print_json(const options_t &options, const coverage_t &coverage)
{
    std::printf(
        "{"
        "\"schema_version\":1,"
        "\"classification\":\"offline_deterministic_parity_corpus\","
        "\"snapshot_count\":%" PRIu64 ","
        "\"seed\":%" PRIu32 ","
        "\"retention_slots\":%" PRIu32 ","
        "\"corpus_digest\":\"%016" PRIx64 "\","
        "\"coverage\":{"
        "\"keyframes\":%" PRIu64 ","
        "\"zero_base_keyframes\":%" PRIu64 ","
        "\"acknowledged_branches\":%" PRIu64 ","
        "\"invalid_base_rejections\":%" PRIu64 ","
        "\"invalid_base_keyframe_recoveries\":%" PRIu64 ","
        "\"entity_adds\":%" PRIu64 ","
        "\"entity_updates\":%" PRIu64 ","
        "\"entity_removes\":%" PRIu64 ","
        "\"entity_reuses\":%" PRIu64 ","
        "\"visibility_gap_snapshots\":%" PRIu64 ","
        "\"visibility_gap_reentries\":%" PRIu64 ","
        "\"truncations\":%" PRIu64 ","
        "\"fragment_stalls\":%" PRIu64 ","
        "\"rate_suppressions\":%" PRIu64 ","
        "\"event_snapshots\":%" PRIu64 ","
        "\"endpoint_hash_matches\":%" PRIu64 ","
        "\"legacy_hash_matches\":%" PRIu64 ","
        "\"component_hash_matches\":%" PRIu64 ","
        "\"unchanged_old_origin_matches\":%" PRIu64 ","
        "\"exact_chronology_matches\":%" PRIu64 ","
        "\"authoritative_tick_wraps\":%" PRIu64 ","
        "\"wire_sequence_boundary_cases\":%" PRIu64 ","
        "\"wire_sequence_wrap_cases\":%" PRIu64 ","
        "\"stale_ref_rejections\":%" PRIu64 "},"
        "\"wire_sequence_domain\":{"
        "\"minimum_serverframe\":0,"
        "\"maximum_serverframe\":2147483647,"
        "\"maximum_snapshot_sequence\":2147483648,"
        "\"wrap_supported\":false,"
        "\"boundary_cases\":%" PRIu32 "}"
        "}\n",
        coverage.snapshots, options.seed, options.retention_slots,
        coverage.digest, coverage.keyframes,
        coverage.zero_base_keyframes, coverage.acknowledged_branches,
        coverage.invalid_base_rejections,
        coverage.invalid_base_keyframe_recoveries, coverage.entity_adds,
        coverage.entity_updates, coverage.entity_removes,
        coverage.entity_reuses, coverage.visibility_gap_snapshots,
        coverage.visibility_gap_reentries, coverage.truncations,
        coverage.fragment_stalls, coverage.rate_suppressions,
        coverage.event_snapshots, coverage.endpoint_hash_matches,
        coverage.legacy_hash_matches, coverage.component_hash_matches,
        coverage.unchanged_old_origin_matches,
        coverage.exact_chronology_matches,
        coverage.authoritative_tick_wraps,
        coverage.wire_sequence_boundary_cases,
        coverage.wire_sequence_wrap_cases,
        coverage.stale_ref_rejections, options.boundary_cases);
}

} // namespace

int main(int argc, char **argv)
{
    const options_t options = parse_options(argc, argv);
    rng_t rng(options.seed);
    coverage_t coverage;
    const uint32_t main_count = options.snapshots - options.boundary_cases;
    run_session(main_count, 0, 101, 0, options, rng, coverage, false);
    const int32_t boundary_start =
        INT32_MAX - static_cast<int32_t>(options.boundary_cases) + 1;
    run_session(options.boundary_cases, boundary_start, 102, main_count,
                options, rng, coverage, true);

    CHECK(coverage.snapshots == options.snapshots);
    CHECK(coverage.endpoint_hash_matches == options.snapshots);
    CHECK(coverage.legacy_hash_matches == options.snapshots);
    CHECK(coverage.component_hash_matches == options.snapshots);
    CHECK(coverage.exact_chronology_matches == options.snapshots);
    CHECK(coverage.wire_sequence_boundary_cases == options.boundary_cases);
    CHECK(coverage.wire_sequence_wrap_cases == 0);
    CHECK(coverage.invalid_base_keyframe_recoveries ==
          coverage.invalid_base_rejections);
    print_json(options, coverage);
    return EXIT_SUCCESS;
}
