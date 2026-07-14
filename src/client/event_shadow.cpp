/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "client.h"

#include "client/cgame_event_shadow_runtime.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace {

static_assert(static_cast<uint32_t>(EV_NONE) == 0);
static_assert(static_cast<uint32_t>(EV_LADDER_STEP) ==
              WORR_CGAME_EVENT_SHADOW_MAX_LEGACY_EVENT);

std::array<worr_cgame_event_shadow_observed_v1,
           WORR_CGAME_EVENT_SHADOW_MAX_ENTITIES>
    observed_entities_v1;
std::array<uint32_t, WORR_CGAME_EVENT_SHADOW_MAX_ENTITIES> seen_markers_v1;
std::array<worr_event_record_v1, WORR_CGAME_EVENT_SHADOW_MAX_RECORDS>
    scratch_records_v1;
std::array<worr_cgame_event_shadow_carrier_v1,
           WORR_CGAME_EVENT_SHADOW_MAX_ENTITIES>
    frame_carriers_v1;
worr_cgame_event_shadow_builder_v1 builder_v1;
const worr_cgame_event_shadow_export_v1 *event_consumer_v1;

std::array<worr_cgame_event_observed_v2,
           WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2>
    observed_entities_v2;
std::array<uint32_t, WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2> seen_markers_v2;
std::array<worr_event_record_v1, WORR_CGAME_EVENT_RANGE_MAX_RECORDS_V2>
    scratch_records_v2;
std::array<worr_cgame_event_carrier_v2,
           WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2>
    frame_carriers_v2;
worr_cgame_event_range_builder_v2 builder_v2;
const worr_cgame_event_range_export_v2 *event_consumer_v2;
uint32_t epoch_seed;
bool builder_v1_initialized;
bool builder_v2_initialized;

void consume_bridge(void *, const worr_cgame_event_range_v1 *range)
{
    if (event_consumer_v1)
        event_consumer_v1->ConsumeCanonicalEventRange(range);
}

void consume_bridge_v2(void *, const worr_cgame_event_range_v2 *range)
{
    if (event_consumer_v2)
        event_consumer_v2->ConsumeCanonicalEventRange(range);
}

bool reset_builder(uint32_t reason)
{
    if (epoch_seed == UINT32_MAX) {
        builder_v1_initialized = false;
        builder_v2_initialized = false;
        return false;
    }
    ++epoch_seed;
    if (!builder_v1_initialized) {
        builder_v1_initialized = Worr_CGameEventShadowBuilderInitV1(
            &builder_v1, observed_entities_v1.data(), seen_markers_v1.data(),
            static_cast<uint32_t>(observed_entities_v1.size()),
            scratch_records_v1.data(),
            static_cast<uint32_t>(scratch_records_v1.size()), epoch_seed);
    } else {
        builder_v1_initialized =
            Worr_CGameEventShadowBuilderResetV1(&builder_v1, epoch_seed);
    }
    if (!builder_v2_initialized) {
        builder_v2_initialized = Worr_CGameEventRangeBuilderInitV2(
            &builder_v2, observed_entities_v2.data(), seen_markers_v2.data(),
            static_cast<uint32_t>(observed_entities_v2.size()),
            scratch_records_v2.data(),
            static_cast<uint32_t>(scratch_records_v2.size()), epoch_seed);
    } else {
        builder_v2_initialized =
            Worr_CGameEventRangeBuilderResetV2(&builder_v2, epoch_seed);
    }
    if (builder_v1_initialized && event_consumer_v1)
        event_consumer_v1->Reset(epoch_seed, reason);
    if (builder_v2_initialized && event_consumer_v2)
        event_consumer_v2->Reset(epoch_seed, reason);
    return builder_v1_initialized || builder_v2_initialized;
}

uint64_t frame_time_us()
{
    if (cl.servertime <= 0)
        return 0;
    return static_cast<uint64_t>(cl.servertime) * UINT64_C(1000);
}

uint32_t action_tick()
{
    return cl.frame.number < 0 ? 0u
                               : static_cast<uint32_t>(cl.frame.number);
}

uint32_t demo_range_flags_v2()
{
    uint32_t flags = 0;
    if (cls.demo.playback)
        flags |= WORR_CGAME_EVENT_RANGE_DEMO_PLAYBACK_V2;
    if (cls.demo.seeking)
        flags |= WORR_CGAME_EVENT_RANGE_DEMO_SEEK_V2;
    return flags;
}

uint32_t runtime_entity_capacity()
{
    const uint32_t configured = static_cast<uint32_t>(cl.csr.max_edicts);
    if (cl.csr.max_edicts <= 1)
        return 0;
    return configured < WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2
               ? configured
               : WORR_CGAME_EVENT_RANGE_MAX_ENTITIES_V2;
}

bool runtime_entity_valid(int32_t entity, bool allow_world)
{
    const uint32_t capacity = runtime_entity_capacity();
    return capacity != 0 && entity >= (allow_world ? 0 : 1) &&
           static_cast<uint32_t>(entity) < capacity;
}

void initialize_action_candidate(
    worr_cgame_event_action_candidate_v2 *candidate,
    uint16_t event_type,
    uint16_t payload_kind,
    uint16_t payload_size)
{
    std::memset(candidate, 0, sizeof(*candidate));
    candidate->struct_size = sizeof(*candidate);
    candidate->source_entity_index = 0;
    candidate->subject_entity_index = WORR_EVENT_NO_ENTITY;
    candidate->record.struct_size = sizeof(candidate->record);
    candidate->record.schema_version = WORR_EVENT_ABI_VERSION;
    candidate->record.model_revision = WORR_EVENT_MODEL_REVISION;
    candidate->record.flags = WORR_EVENT_FLAG_REPLAY_SAFE |
                              WORR_EVENT_FLAG_PRESENT_ONCE;
    candidate->record.source_tick = action_tick();
    candidate->record.source_time_us = frame_time_us();
    candidate->record.source_entity.index = WORR_EVENT_NO_ENTITY;
    candidate->record.subject_entity.index = WORR_EVENT_NO_ENTITY;
    candidate->record.event_type = event_type;
    candidate->record.delivery_class = WORR_EVENT_DELIVERY_TRANSIENT;
    candidate->record.prediction_class =
        WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY;
    candidate->record.expiry_tick = candidate->record.source_tick + 1u;
    candidate->record.payload_kind = payload_kind;
    candidate->record.payload_size = payload_size;
}

void deliver_rejected_action(uint32_t carrier_kind, uint32_t adapter_status)
{
    if (!builder_v2_initialized &&
        !reset_builder(WORR_CGAME_EVENT_SHADOW_RESET_CLIENT_STATE)) {
        return;
    }
    if (!builder_v2_initialized)
        return;
    (void)Worr_CGameEventRangeDeliverRejectedActionV2(
        &builder_v2, action_tick(), frame_time_us(), carrier_kind,
        adapter_status, demo_range_flags_v2(), consume_bridge_v2, nullptr);
}

void deliver_action_candidate(
    const worr_cgame_event_action_candidate_v2 *candidate,
    uint32_t carrier_kind)
{
    worr_cgame_event_range_build_result_v2 result;
    if (!builder_v2_initialized &&
        !reset_builder(WORR_CGAME_EVENT_SHADOW_RESET_CLIENT_STATE)) {
        return;
    }
    if (!builder_v2_initialized)
        return;
    result = Worr_CGameEventRangeDeliverActionV2(
        &builder_v2, candidate, carrier_kind, demo_range_flags_v2(),
        consume_bridge_v2, nullptr);
    if (result == WORR_CGAME_EVENT_RANGE_BUILD_INVALID_V2) {
        deliver_rejected_action(
            carrier_kind, WORR_CGAME_EVENT_ADAPTER_PAYLOAD_INVALID_V2);
    }
}

bool temp_entity1_is_source(uint16_t subtype)
{
    switch (subtype) {
    case WORR_EVENT_LEGACY_TEMP_PARASITE_ATTACK:
    case WORR_EVENT_LEGACY_TEMP_MEDIC_CABLE_ATTACK:
    case WORR_EVENT_LEGACY_TEMP_GRAPPLE_CABLE:
    case WORR_EVENT_LEGACY_TEMP_LIGHTNING:
    case WORR_EVENT_LEGACY_TEMP_FLASHLIGHT:
    case WORR_EVENT_LEGACY_TEMP_HEATBEAM:
    case WORR_EVENT_LEGACY_TEMP_MONSTER_HEATBEAM:
    case WORR_EVENT_LEGACY_TEMP_GRAPPLE_CABLE2:
    case WORR_EVENT_LEGACY_TEMP_POWER_SPLASH:
    case WORR_EVENT_LEGACY_TEMP_LIGHTNING_BEAM:
        return true;
    default:
        return false;
    }
}

bool build_temp_candidate(
    const q2proto_svc_temp_entity_t *temp_entity,
    worr_cgame_event_action_candidate_v2 *candidate,
    uint32_t *adapter_status)
{
    worr_event_payload_legacy_temp_v1 payload{};
    uint16_t fields;
    const uint16_t subtype = temp_entity->type;

    if (!Worr_EventLegacyTempFieldMaskV1(
            subtype, temp_entity->entity1, &fields)) {
        *adapter_status =
            subtype == WORR_EVENT_LEGACY_TEMP_STEAM
                ? WORR_CGAME_EVENT_ADAPTER_INVALID_SHAPE_V2
                : WORR_CGAME_EVENT_ADAPTER_UNSUPPORTED_ID_V2;
        return false;
    }
    if (temp_entity1_is_source(subtype) &&
        !runtime_entity_valid(temp_entity->entity1, true)) {
        *adapter_status =
            WORR_CGAME_EVENT_ADAPTER_ENTITY_OUT_OF_RANGE_V2;
        return false;
    }
    if (subtype == WORR_EVENT_LEGACY_TEMP_LIGHTNING &&
        !runtime_entity_valid(temp_entity->entity2, true)) {
        *adapter_status =
            WORR_CGAME_EVENT_ADAPTER_ENTITY_OUT_OF_RANGE_V2;
        return false;
    }

    payload.subtype = subtype;
    payload.valid_fields = fields;
    if (fields & WORR_EVENT_LEGACY_TEMP_FIELD_ENTITY1)
        payload.raw_entity1 = temp_entity->entity1;
    if (fields & WORR_EVENT_LEGACY_TEMP_FIELD_ENTITY2)
        payload.raw_entity2 = temp_entity->entity2;
    if (fields & WORR_EVENT_LEGACY_TEMP_FIELD_TIME)
        payload.time_ms = temp_entity->time;
    if (fields & WORR_EVENT_LEGACY_TEMP_FIELD_COUNT)
        payload.count_or_amount = temp_entity->count;
    if (fields & WORR_EVENT_LEGACY_TEMP_FIELD_COLOR)
        payload.color = temp_entity->color;
    if (fields & WORR_EVENT_LEGACY_TEMP_FIELD_POSITION1)
        std::memcpy(payload.position1, temp_entity->position1,
                    sizeof(payload.position1));
    if (fields & WORR_EVENT_LEGACY_TEMP_FIELD_POSITION2)
        std::memcpy(payload.position2, temp_entity->position2,
                    sizeof(payload.position2));
    if (fields & WORR_EVENT_LEGACY_TEMP_FIELD_OFFSET)
        std::memcpy(payload.offset, temp_entity->offset,
                    sizeof(payload.offset));
    if (fields & WORR_EVENT_LEGACY_TEMP_FIELD_DIRECTION)
        std::memcpy(payload.direction, temp_entity->direction,
                    sizeof(payload.direction));

    initialize_action_candidate(
        candidate,
        subtype == WORR_EVENT_LEGACY_TEMP_DAMAGE_DEALT
            ? WORR_EVENT_TYPE_GAMEPLAY_CUE
            : WORR_EVENT_TYPE_VISUAL_EFFECT,
        WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1,
        static_cast<uint16_t>(sizeof(payload)));
    if (temp_entity1_is_source(subtype)) {
        candidate->source_entity_index =
            static_cast<uint32_t>(temp_entity->entity1);
    }
    if (subtype == WORR_EVENT_LEGACY_TEMP_LIGHTNING) {
        candidate->subject_entity_index =
            static_cast<uint32_t>(temp_entity->entity2);
    }
    std::memcpy(candidate->record.payload, &payload, sizeof(payload));
    *adapter_status = WORR_CGAME_EVENT_ADAPTER_OK_V2;
    return true;
}

uint16_t muzzle_event_type(uint32_t family, uint32_t flash_id)
{
    if (family == WORR_EVENT_MUZZLE_FAMILY_MONSTER)
        return WORR_EVENT_TYPE_WEAPON_FIRE;
    if (flash_id >= WORR_EVENT_PLAYER_MUZZLE_LOGIN &&
        flash_id <= WORR_EVENT_PLAYER_MUZZLE_RESPAWN) {
        return WORR_EVENT_TYPE_STATE_CHANGE;
    }
    if (flash_id == WORR_EVENT_PLAYER_MUZZLE_ITEM_RESPAWN ||
        (flash_id >= WORR_EVENT_PLAYER_MUZZLE_NUKE1 &&
         flash_id <= WORR_EVENT_PLAYER_MUZZLE_NUKE8)) {
        return WORR_EVENT_TYPE_VISUAL_EFFECT;
    }
    return WORR_EVENT_TYPE_WEAPON_FIRE;
}

} // namespace

extern "C" void CL_EventShadowSetConsumer(
    const worr_cgame_event_shadow_export_v1 *consumer)
{
    event_consumer_v1 = consumer;
    if (!event_consumer_v1)
        return;
    if (!builder_v1_initialized) {
        (void)reset_builder(
            WORR_CGAME_EVENT_SHADOW_RESET_CONSUMER_ATTACH);
        return;
    }
    event_consumer_v1->Reset(
        builder_v1.stream_epoch,
        WORR_CGAME_EVENT_SHADOW_RESET_CONSUMER_ATTACH);
}

extern "C" void CL_EventRangeSetConsumerV2(
    const worr_cgame_event_range_export_v2 *consumer)
{
    event_consumer_v2 = consumer;
    if (!event_consumer_v2)
        return;
    if (!builder_v2_initialized) {
        (void)reset_builder(
            WORR_CGAME_EVENT_SHADOW_RESET_CONSUMER_ATTACH);
        return;
    }
    event_consumer_v2->Reset(
        builder_v2.stream_epoch,
        WORR_CGAME_EVENT_SHADOW_RESET_CONSUMER_ATTACH);
}

extern "C" void CL_EventShadowReset(uint32_t reason)
{
    (void)reset_builder(reason);
}

extern "C" void CL_EventShadowDeliverAcceptedFrame(void)
{
    uint32_t index;
    uint32_t carrier_count;
    uint32_t flags_v1 = 0;
    uint32_t flags_v2;

    if ((!builder_v1_initialized || !builder_v2_initialized) &&
        !reset_builder(WORR_CGAME_EVENT_SHADOW_RESET_CLIENT_STATE)) {
        return;
    }
    if (!cl.frame.valid || cl.frame.number < 0 ||
        cl.frame.numEntities < 0 ||
        static_cast<uint32_t>(cl.frame.numEntities) >
            frame_carriers_v1.size()) {
        return;
    }

    carrier_count = static_cast<uint32_t>(cl.frame.numEntities);
    for (index = 0; index < carrier_count; ++index) {
        const uint32_t parse_index =
            (static_cast<uint32_t>(cl.frame.firstEntity) + index) &
            PARSE_ENTITIES_MASK;
        const entity_state_t &state = cl.entityStates[parse_index];
        frame_carriers_v1[index].entity_index =
            static_cast<uint32_t>(state.number);
        const uint32_t raw_event = static_cast<uint32_t>(state.event);
        frame_carriers_v1[index].raw_event =
            raw_event <= WORR_CGAME_EVENT_SHADOW_MAX_LEGACY_EVENT
                ? raw_event
                : 0u;
        frame_carriers_v1[index].scan_order = index;
        frame_carriers_v2[index].entity_index =
            static_cast<uint32_t>(state.number);
        frame_carriers_v2[index].raw_event = raw_event;
        frame_carriers_v2[index].scan_order = index;
    }
    if (cls.demo.playback)
        flags_v1 |= WORR_CGAME_EVENT_SHADOW_RANGE_DEMO_PLAYBACK;
    flags_v2 = demo_range_flags_v2();

    const uint32_t source_tick = static_cast<uint32_t>(cl.frame.number);
    if ((builder_v1_initialized && builder_v1.has_last_source_tick &&
         source_tick < builder_v1.last_source_tick) ||
        (builder_v2_initialized && builder_v2.has_last_frame_tick &&
         source_tick < builder_v2.last_frame_tick)) {
        if (!reset_builder(WORR_CGAME_EVENT_SHADOW_RESET_FRAME_REWIND))
            return;
    }
    if (builder_v1_initialized) {
        (void)Worr_CGameEventShadowDeliverFrameV1(
            &builder_v1, source_tick, frame_time_us(),
            frame_carriers_v1.data(), carrier_count, flags_v1,
            consume_bridge, nullptr);
    }
    if (builder_v2_initialized) {
        (void)Worr_CGameEventRangeDeliverFrameV2(
            &builder_v2, source_tick, frame_time_us(),
            frame_carriers_v2.data(), carrier_count, flags_v2,
            consume_bridge_v2, nullptr);
    }
}

extern "C" void CL_EventRangeCaptureTempV2(
    const q2proto_svc_temp_entity_t *temp_entity)
{
    worr_cgame_event_action_candidate_v2 candidate;
    uint32_t adapter_status;
    if (!temp_entity)
        return;
    if (!build_temp_candidate(temp_entity, &candidate, &adapter_status)) {
        deliver_rejected_action(
            WORR_CGAME_EVENT_CARRIER_TEMP_ENTITY_V2, adapter_status);
        return;
    }
    deliver_action_candidate(
        &candidate, WORR_CGAME_EVENT_CARRIER_TEMP_ENTITY_V2);
}

extern "C" void CL_EventRangeCaptureMuzzleV2(
    const q2proto_svc_muzzleflash_t *muzzleflash,
    uint32_t family)
{
    worr_cgame_event_action_candidate_v2 candidate;
    worr_event_payload_muzzle_v1 payload{};
    uint32_t carrier_kind;

    if (!muzzleflash)
        return;
    carrier_kind = family == WORR_EVENT_MUZZLE_FAMILY_PLAYER
                       ? WORR_CGAME_EVENT_CARRIER_PLAYER_MUZZLE_V2
                       : WORR_CGAME_EVENT_CARRIER_MONSTER_MUZZLE_V2;
    if (!Worr_EventMuzzleCarrierValidV1(
            family, muzzleflash->entity, muzzleflash->weapon,
            runtime_entity_capacity(),
            WORR_EVENT_MONSTER_MUZZLE_LAST + 1u) ||
        (family == WORR_EVENT_MUZZLE_FAMILY_MONSTER &&
         muzzleflash->silenced)) {
        deliver_rejected_action(
            carrier_kind, WORR_CGAME_EVENT_ADAPTER_INVALID_SHAPE_V2);
        return;
    }

    payload.family = static_cast<uint16_t>(family);
    payload.flash_id = muzzleflash->weapon;
    if (muzzleflash->silenced)
        payload.flags = WORR_EVENT_MUZZLE_FLAG_SILENCED;
    initialize_action_candidate(
        &candidate, muzzle_event_type(family, muzzleflash->weapon),
        WORR_EVENT_PAYLOAD_MUZZLE_V1,
        static_cast<uint16_t>(sizeof(payload)));
    candidate.source_entity_index =
        static_cast<uint32_t>(muzzleflash->entity);
    std::memcpy(candidate.record.payload, &payload, sizeof(payload));
    deliver_action_candidate(&candidate, carrier_kind);
}

extern "C" void CL_EventRangeCaptureSoundV2(const q2proto_sound_t *sound)
{
    worr_cgame_event_action_candidate_v2 candidate;
    worr_event_payload_spatial_audio_v1 payload{};

    if (!sound)
        return;
    if (sound->index <= 0 ||
        (sound->has_entity_channel &&
         !runtime_entity_valid(sound->entity, true))) {
        deliver_rejected_action(
            WORR_CGAME_EVENT_CARRIER_SPATIAL_SOUND_V2,
            sound->has_entity_channel
                ? WORR_CGAME_EVENT_ADAPTER_ENTITY_OUT_OF_RANGE_V2
                : WORR_CGAME_EVENT_ADAPTER_INVALID_SHAPE_V2);
        return;
    }

    payload.asset_id = static_cast<uint32_t>(sound->index);
    payload.raw_entity = WORR_EVENT_NO_ENTITY;
    payload.volume = sound->volume;
    payload.attenuation = sound->attenuation;
    payload.time_offset = sound->timeofs;
    payload.pitch = 1.0f;
    if (sound->has_entity_channel) {
        if (sound->channel < 0 || sound->channel > UINT16_MAX) {
            deliver_rejected_action(
                WORR_CGAME_EVENT_CARRIER_SPATIAL_SOUND_V2,
                WORR_CGAME_EVENT_ADAPTER_INVALID_SHAPE_V2);
            return;
        }
        payload.flags |= WORR_EVENT_SPATIAL_AUDIO_HAS_ENTITY_CHANNEL;
        payload.channel = static_cast<uint16_t>(sound->channel);
        payload.raw_entity = static_cast<uint32_t>(sound->entity);
    }
    if (sound->has_position) {
        payload.flags |= WORR_EVENT_SPATIAL_AUDIO_HAS_POSITION;
        std::memcpy(payload.origin, sound->pos, sizeof(payload.origin));
    }

    initialize_action_candidate(
        &candidate, WORR_EVENT_TYPE_AUDIO_CUE,
        WORR_EVENT_PAYLOAD_SPATIAL_AUDIO_V1,
        static_cast<uint16_t>(sizeof(payload)));
    candidate.source_entity_index =
        sound->has_entity_channel
            ? static_cast<uint32_t>(sound->entity)
            : 0u;
    std::memcpy(candidate.record.payload, &payload, sizeof(payload));
    deliver_action_candidate(
        &candidate, WORR_CGAME_EVENT_CARRIER_SPATIAL_SOUND_V2);
}
