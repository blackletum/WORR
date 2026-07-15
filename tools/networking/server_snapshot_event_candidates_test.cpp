/* Standalone FR-10-T05/T06 final-emission event candidate tests. */

#include "common/net/legacy_entity_event_candidate.h"
#include "server/snapshot_event_candidates.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr uint32_t max_entities = 16;
constexpr uint32_t slot_count = 2;
constexpr uint32_t entities_per_slot = 8;
constexpr uint32_t area_bytes_per_slot = 4;

[[noreturn]] void fail(const char *expression, int line) {
  std::fprintf(stderr, "server_snapshot_event_candidates_test:%d: %s\n", line,
               expression);
  std::exit(EXIT_FAILURE);
}

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression))                                                         \
      fail(#expression, __LINE__);                                             \
  } while (0)

struct expected_mapping_t {
  uint16_t raw_event;
  uint16_t event_type;
  uint16_t payload_flags;
};

constexpr std::array<expected_mapping_t, 9> expected_mappings{{
    {WORR_EVENT_LEGACY_ENTITY_ITEM_RESPAWN, WORR_EVENT_TYPE_VISUAL_EFFECT,
     WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION},
    {WORR_EVENT_LEGACY_ENTITY_FOOTSTEP, WORR_EVENT_TYPE_MOVEMENT_CUE,
     WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION},
    {WORR_EVENT_LEGACY_ENTITY_FALL_SHORT, WORR_EVENT_TYPE_MOVEMENT_CUE,
     WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION},
    {WORR_EVENT_LEGACY_ENTITY_FALL_MEDIUM, WORR_EVENT_TYPE_MOVEMENT_CUE,
     WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION},
    {WORR_EVENT_LEGACY_ENTITY_FALL_FAR, WORR_EVENT_TYPE_MOVEMENT_CUE,
     WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION},
    {WORR_EVENT_LEGACY_ENTITY_PLAYER_TELEPORT, WORR_EVENT_TYPE_STATE_CHANGE,
     WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION |
         WORR_EVENT_LEGACY_ENTITY_FLAG_DISCONTINUITY},
    {WORR_EVENT_LEGACY_ENTITY_OTHER_TELEPORT, WORR_EVENT_TYPE_STATE_CHANGE,
     WORR_EVENT_LEGACY_ENTITY_FLAG_DISCONTINUITY},
    {WORR_EVENT_LEGACY_ENTITY_OTHER_FOOTSTEP, WORR_EVENT_TYPE_MOVEMENT_CUE,
     WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION},
    {WORR_EVENT_LEGACY_ENTITY_LADDER_STEP, WORR_EVENT_TYPE_MOVEMENT_CUE,
     WORR_EVENT_LEGACY_ENTITY_FLAG_PRESENTATION},
}};

worr_event_payload_legacy_entity_v1
payload(const worr_event_record_v1 &record) {
  worr_event_payload_legacy_entity_v1 result{};
  std::memcpy(&result, record.payload, sizeof(result));
  return result;
}

void test_canonical_constructor_catalog_and_atomic_failure() {
  for (const auto &expected : expected_mappings) {
    worr_event_record_v1 candidate{};
    uint64_t semantic_hash = 0;
    CHECK(Worr_LegacyEntityEventCandidateBuildV1(
        17, UINT64_C(425000), 3, {5, 7}, expected.raw_event, max_entities,
        &candidate, &semantic_hash));
    CHECK(Worr_EventRecordCandidateValidateV1(&candidate, max_entities));
    CHECK(candidate.source_tick == 17 &&
          candidate.source_time_us == UINT64_C(425000) &&
          candidate.source_ordinal == 3);
    CHECK(candidate.source_entity.index == 5 &&
          candidate.source_entity.generation == 7);
    CHECK(candidate.subject_entity.index == WORR_EVENT_NO_ENTITY &&
          candidate.subject_entity.generation == 0);
    CHECK(candidate.event_type == expected.event_type &&
          candidate.delivery_class == WORR_EVENT_DELIVERY_TRANSIENT &&
          candidate.prediction_class ==
              WORR_EVENT_PREDICTION_AUTHORITATIVE_ONLY);
    CHECK(candidate.expiry_tick == 18 && semantic_hash != 0);
    const auto actual_payload = payload(candidate);
    CHECK(actual_payload.raw_event == expected.raw_event &&
          actual_payload.flags == expected.payload_flags &&
          actual_payload.reserved0 == 0);
  }

  worr_event_record_v1 sentinel;
  std::memset(&sentinel, 0xa5, sizeof(sentinel));
  const auto sentinel_before = sentinel;
  uint64_t semantic_hash = UINT64_C(0x1122334455667788);
  CHECK(!Worr_LegacyEntityEventCandidateBuildV1(
      17, 425000, 3, {5, 7}, 0, max_entities, &sentinel, &semantic_hash));
  CHECK(std::memcmp(&sentinel, &sentinel_before, sizeof(sentinel)) == 0);
  CHECK(semantic_hash == UINT64_C(0x1122334455667788));
  CHECK(!Worr_LegacyEntityEventCandidateBuildV1(
      17, 425000, 3, {max_entities, 1}, WORR_EVENT_LEGACY_ENTITY_FOOTSTEP,
      max_entities, &sentinel, &semantic_hash));
  CHECK(std::memcmp(&sentinel, &sentinel_before, sizeof(sentinel)) == 0);
}

q2proto_svc_playerstate_t full_player() {
  q2proto_svc_playerstate_t player{};
  player.delta_bits = Q2P_PSD_PM_GRAVITY | Q2P_PSD_FOV | Q2P_PSD_PM_VIEWHEIGHT;
  player.pm_gravity = 800;
  player.pm_viewheight = 22;
  player.fov = 100;
  return player;
}

q2proto_entity_state_delta_t entity_delta(uint16_t model, uint16_t frame,
                                          uint8_t event = 0) {
  q2proto_entity_state_delta_t delta{};
  delta.delta_bits = Q2P_ESD_MODELINDEX | Q2P_ESD_FRAME;
  delta.modelindex = model;
  delta.frame = frame;
  if (event != 0) {
    delta.delta_bits |= Q2P_ESD_EVENT;
    delta.event = event;
  }
  return delta;
}

q2proto_entity_state_delta_t event_delta(uint8_t event) {
  q2proto_entity_state_delta_t delta{};
  delta.delta_bits = Q2P_ESD_EVENT;
  delta.event = event;
  return delta;
}

struct frame_carrier_t {
  q2proto_svc_frame_t frame{};
  std::array<q2proto_svc_frame_entity_delta_t, 8> deltas{};
  uint32_t count = 1;
  std::array<uint8_t, area_bytes_per_slot> area{1, 2, 3, 4};
};

frame_carrier_t make_frame(int wire_frame, int wire_base, bool full_ps) {
  frame_carrier_t result{};
  result.frame.serverframe = wire_frame;
  result.frame.deltaframe = wire_base;
  result.frame.areabits = result.area.data();
  result.frame.areabits_len = result.area.size();
  if (full_ps)
    result.frame.playerstate = full_player();
  return result;
}

void add_delta(frame_carrier_t &frame, uint16_t entity,
               const q2proto_entity_state_delta_t &delta, bool remove = false) {
  CHECK(frame.count < frame.deltas.size());
  auto &carrier = frame.deltas[frame.count - 1u];
  carrier.newnum = entity;
  carrier.remove = remove;
  carrier.entity_delta = delta;
  ++frame.count;
  frame.deltas[frame.count - 1u] = {};
}

sv_snapshot_shadow_config_v1 config() {
  sv_snapshot_shadow_config_v1 result{};
  result.struct_size = sizeof(result);
  result.schema_version = SV_SNAPSHOT_SHADOW_VERSION;
  result.snapshot_epoch = 31;
  result.max_entities = max_entities;
  result.max_models = 64;
  result.max_sounds = 64;
  result.slot_capacity = slot_count;
  result.entities_per_slot = entities_per_slot;
  result.area_bytes_per_slot = area_bytes_per_slot;
  result.beam_renderfx_mask = UINT32_C(1) << 7;
  result.legacy_renderfx_allowed_mask = (UINT32_C(1) << 19) - 1u;
  result.legacy_beam_clear_mask = UINT32_C(1) << 9;
  result.extended_entity_state = 1;
  return result;
}

sv_snapshot_shadow_ref_v1 send(sv_snapshot_shadow_peer_v1 *peer,
                               frame_carrier_t &carrier,
                               uint32_t authoritative_tick,
                               uint64_t authoritative_time_us) {
  sv_snapshot_shadow_frame_v1 input{};
  input.struct_size = sizeof(input);
  input.schema_version = SV_SNAPSHOT_SHADOW_VERSION;
  input.wire_frame = &carrier.frame;
  input.authoritative_server_tick = authoritative_tick;
  input.authoritative_tick_delta = 1;
  input.authoritative_server_time_us = authoritative_time_us;
  input.controlled_entity_index = 1;
  CHECK(SV_SnapshotShadowBeginFrameV1(peer, &input) == SV_SNAPSHOT_SHADOW_OK);
  for (uint32_t i = 0; i < carrier.count; ++i) {
    CHECK(SV_SnapshotShadowCaptureEntityDeltaV1(peer, &carrier.deltas[i]) ==
          SV_SNAPSHOT_SHADOW_OK);
  }
  sv_snapshot_shadow_ref_v1 ref{UINT32_MAX, UINT32_MAX};
  CHECK(SV_SnapshotShadowCommitFrameV1(peer, &ref) == SV_SNAPSHOT_SHADOW_OK);
  return ref;
}

void verify_semantic_refs(sv_snapshot_shadow_peer_v1 *peer,
                          sv_snapshot_shadow_ref_v1 ref,
                          const worr_event_record_v1 *candidates,
                          uint32_t count) {
  worr_snapshot_projection_view_v2 view{};
  worr_snapshot_projection_hashes_v2 hashes{};
  CHECK(SV_SnapshotShadowViewV1(peer, ref, &view, &hashes) ==
        SV_SNAPSHOT_SHADOW_OK);
  CHECK(view.event_ref_count == count);
  for (uint32_t i = 0; i < count; ++i) {
    uint64_t semantic_hash = 0;
    CHECK(Worr_EventRecordSemanticHashV1(&candidates[i], max_entities,
                                         &semantic_hash));
    CHECK(view.event_refs[i].carrier_ordinal == i &&
          view.event_refs[i].semantic_hash == semantic_hash);
  }
}

void test_final_per_peer_retention_capacity_and_stale_refs() {
  const auto shadow_config = config();
  auto *peer = SV_SnapshotShadowCreateV1(&shadow_config);
  CHECK(peer != nullptr);

  auto frame10 = make_frame(10, -1, true);
  add_delta(frame10, 2, entity_delta(2, 1, WORR_EVENT_LEGACY_ENTITY_FOOTSTEP));
  add_delta(frame10, 4, entity_delta(4, 1));
  add_delta(frame10, 7,
            entity_delta(7, 1, WORR_EVENT_LEGACY_ENTITY_PLAYER_TELEPORT));
  const auto ref10 = send(peer, frame10, 1000, UINT64_C(25000000));

  std::array<worr_event_record_v1, 2> candidates;
  std::memset(candidates.data(), 0x5a,
              sizeof(candidates[0]) * candidates.size());
  const auto candidates_before = candidates;
  uint32_t count = UINT32_C(0xdeadbeef);
  CHECK(
      SV_SnapshotShadowCopyEventCandidatesV1(peer, ref10, nullptr, 0, &count) ==
      SV_SNAPSHOT_EVENT_CANDIDATES_CAPACITY);
  CHECK(count == 2);
  count = UINT32_C(0xdeadbeef);
  CHECK(SV_SnapshotShadowCopyEventCandidatesV1(peer, ref10, candidates.data(),
                                               1, &count) ==
        SV_SNAPSHOT_EVENT_CANDIDATES_CAPACITY);
  CHECK(count == 2 && std::memcmp(candidates.data(), candidates_before.data(),
                                  sizeof(candidates)) == 0);
  CHECK(SV_SnapshotShadowCopyEventCandidatesV1(peer, ref10, candidates.data(),
                                               candidates.size(), &count) ==
        SV_SNAPSHOT_EVENT_CANDIDATES_OK);
  CHECK(count == 2);

  /* The inferred semantic tick remains the exact per-peer wire tick used by
   * the q2proto projector, while the retained snapshot exposes the distinct
   * authoritative simulation tick. */
  CHECK(candidates[0].source_tick == 10 && candidates[0].source_tick != 1000 &&
        candidates[0].source_time_us == UINT64_C(25000000));
  CHECK(candidates[0].source_ordinal == 0 &&
        candidates[0].source_entity.index == 2 &&
        candidates[0].source_entity.generation == 1);
  CHECK(candidates[1].source_ordinal == 2 &&
        candidates[1].source_entity.index == 7 &&
        candidates[1].source_entity.generation == 1);
  CHECK(payload(candidates[0]).raw_event == WORR_EVENT_LEGACY_ENTITY_FOOTSTEP);
  CHECK(payload(candidates[1]).raw_event ==
        WORR_EVENT_LEGACY_ENTITY_PLAYER_TELEPORT);
  verify_semantic_refs(peer, ref10, candidates.data(), count);

  auto frame11 = make_frame(11, 10, false);
  const auto ref11 = send(peer, frame11, 1001, UINT64_C(25025000));
  count = UINT32_MAX;
  CHECK(
      SV_SnapshotShadowCopyEventCandidatesV1(peer, ref11, nullptr, 0, &count) ==
      SV_SNAPSHOT_EVENT_CANDIDATES_OK);
  CHECK(count == 0);

  /* Branch from frame 10: only the event accepted in this peer's exact
   * final delta list is retained; prior transient events do not carry. */
  auto frame12 = make_frame(12, 10, false);
  add_delta(frame12, 4, event_delta(WORR_EVENT_LEGACY_ENTITY_OTHER_TELEPORT));
  const auto ref12 = send(peer, frame12, 1002, UINT64_C(25050000));
  worr_event_record_v1 branch_candidate{};
  count = 0;
  CHECK(SV_SnapshotShadowCopyEventCandidatesV1(peer, ref12, &branch_candidate,
                                               1, &count) ==
        SV_SNAPSHOT_EVENT_CANDIDATES_OK);
  CHECK(count == 1 && branch_candidate.source_tick == 12 &&
        branch_candidate.source_ordinal == 1 &&
        branch_candidate.source_entity.index == 4 &&
        payload(branch_candidate).raw_event ==
            WORR_EVENT_LEGACY_ENTITY_OTHER_TELEPORT);
  verify_semantic_refs(peer, ref12, &branch_candidate, 1);

  /* ref12 overwrote ref10's two-slot arena.  Generation validation must
   * reject ref10 without copying ref12's compact source metadata. */
  std::memset(candidates.data(), 0x3c,
              sizeof(candidates[0]) * candidates.size());
  const auto stale_before = candidates;
  count = UINT32_C(0xabcdef01);
  CHECK(SV_SnapshotShadowCopyEventCandidatesV1(peer, ref10, candidates.data(),
                                               candidates.size(), &count) ==
        SV_SNAPSHOT_EVENT_CANDIDATES_STALE_REF);
  CHECK(count == UINT32_C(0xabcdef01) &&
        std::memcmp(candidates.data(), stale_before.data(),
                    sizeof(candidates)) == 0);

  sv_snapshot_shadow_sent_v1 sent12{};
  CHECK(SV_SnapshotShadowGetSentV1(peer, ref12, &sent12) ==
        SV_SNAPSHOT_SHADOW_OK);
  CHECK(sent12.snapshot.server_tick == 1002 &&
        sent12.wire_snapshot_number == 12);

  SV_SnapshotShadowDestroyV1(peer);
}

void test_visible_spatial_audio_final_emission_binding() {
  const auto shadow_config = config();
  auto *peer = SV_SnapshotShadowCreateV1(&shadow_config);
  CHECK(peer != nullptr);

  auto frame = make_frame(31, -1, true);
  add_delta(frame, 2, entity_delta(2, 1));
  add_delta(frame, 4, entity_delta(4, 1));
  const auto ref = send(peer, frame, 2000, UINT64_C(50000000));

  q2proto_sound_t sound{};
  sound.index = 17;
  sound.has_entity_channel = true;
  sound.entity = 4;
  sound.channel = 2;
  sound.volume = 0.75f;
  sound.attenuation = 1.0f;
  sound.timeofs = 0.125f;

  worr_event_record_v1 candidate{};
  CHECK(SV_SnapshotShadowBuildSpatialAudioCandidateV1(
            peer, ref, max_entities, &sound, &candidate) ==
        SV_SNAPSHOT_EVENT_CANDIDATES_OK);
  CHECK(Worr_EventRecordCandidateValidateV1(&candidate, max_entities));
  CHECK(candidate.source_tick == 31 &&
        candidate.source_time_us == UINT64_C(50000000) &&
        candidate.source_entity.index == 4 &&
        candidate.source_entity.generation == 1 && candidate.source_ordinal == 0);
  worr_event_payload_spatial_audio_v1 payload{};
  std::memcpy(&payload, candidate.payload, sizeof(payload));
  CHECK(candidate.event_type == WORR_EVENT_TYPE_AUDIO_CUE &&
        payload.asset_id == 17 && payload.raw_entity == 4 &&
        payload.channel == 2 &&
        (payload.flags & WORR_EVENT_SPATIAL_AUDIO_HAS_ENTITY_CHANNEL) != 0 &&
        (payload.flags & WORR_EVENT_SPATIAL_AUDIO_HAS_POSITION) == 0);

  auto candidate_before = candidate;
  sound.entity = 5;
  CHECK(SV_SnapshotShadowBuildSpatialAudioCandidateV1(
            peer, ref, max_entities, &sound, &candidate) ==
        SV_SNAPSHOT_EVENT_CANDIDATES_SOURCE_NOT_VISIBLE);
  CHECK(std::memcmp(&candidate, &candidate_before, sizeof(candidate)) == 0);

  sound.has_position = true;
  sound.pos[0] = 12.0f;
  sound.pos[1] = -3.0f;
  sound.pos[2] = 7.0f;
  CHECK(SV_SnapshotShadowBuildSpatialAudioCandidateV1(
            peer, ref, max_entities, &sound, &candidate) ==
        SV_SNAPSHOT_EVENT_CANDIDATES_OK);
  CHECK(candidate.source_entity.index == 0 &&
        candidate.source_entity.generation == 1);
  std::memcpy(&payload, candidate.payload, sizeof(payload));
  CHECK(payload.raw_entity == 5 && payload.origin[0] == 12.0f &&
        (payload.flags & WORR_EVENT_SPATIAL_AUDIO_HAS_ENTITY_CHANNEL) != 0 &&
        (payload.flags & WORR_EVENT_SPATIAL_AUDIO_HAS_POSITION) != 0 &&
        (payload.flags & WORR_EVENT_SPATIAL_AUDIO_POSITION_FORCED) != 0);

  candidate_before = candidate;
  sound.entity = 4;
  sound.has_position = false;
  sound.has_entity_channel = false;
  CHECK(SV_SnapshotShadowBuildSpatialAudioCandidateV1(
            peer, ref, max_entities, &sound, &candidate) ==
        SV_SNAPSHOT_EVENT_CANDIDATES_INVALID_ARGUMENT);
  CHECK(std::memcmp(&candidate, &candidate_before, sizeof(candidate)) == 0);

  SV_SnapshotShadowDestroyV1(peer);
}

void test_visible_muzzle_final_emission_binding() {
  const auto shadow_config = config();
  auto *peer = SV_SnapshotShadowCreateV1(&shadow_config);
  CHECK(peer != nullptr);

  auto frame = make_frame(37, -1, true);
  add_delta(frame, 3, entity_delta(3, 1));
  add_delta(frame, 7, entity_delta(7, 1));
  const auto ref = send(peer, frame, 2006, UINT64_C(56000000));

  q2proto_svc_muzzleflash_t muzzleflash{};
  muzzleflash.entity = 7;
  muzzleflash.weapon = WORR_EVENT_PLAYER_MUZZLE_MACHINEGUN;
  muzzleflash.silenced = true;

  worr_event_record_v1 candidate{};
  CHECK(SV_SnapshotShadowBuildMuzzleCandidateV1(
            peer, ref, max_entities, &muzzleflash,
            WORR_EVENT_MUZZLE_FAMILY_PLAYER, &candidate) ==
        SV_SNAPSHOT_EVENT_CANDIDATES_OK);
  CHECK(Worr_EventRecordCandidateValidateV1(&candidate, max_entities));
  CHECK(candidate.source_tick == 37 &&
        candidate.source_time_us == UINT64_C(56000000) &&
        candidate.source_entity.index == 7 &&
        candidate.source_entity.generation == 1 && candidate.source_ordinal == 0);
  worr_event_payload_muzzle_v1 payload{};
  std::memcpy(&payload, candidate.payload, sizeof(payload));
  CHECK(candidate.event_type == WORR_EVENT_TYPE_WEAPON_FIRE &&
        payload.family == WORR_EVENT_MUZZLE_FAMILY_PLAYER &&
        payload.flash_id == WORR_EVENT_PLAYER_MUZZLE_MACHINEGUN &&
        payload.flags == WORR_EVENT_MUZZLE_FLAG_SILENCED);

  muzzleflash.entity = 6;
  const auto candidate_before = candidate;
  CHECK(SV_SnapshotShadowBuildMuzzleCandidateV1(
            peer, ref, max_entities, &muzzleflash,
            WORR_EVENT_MUZZLE_FAMILY_PLAYER, &candidate) ==
        SV_SNAPSHOT_EVENT_CANDIDATES_SOURCE_NOT_VISIBLE);
  CHECK(std::memcmp(&candidate, &candidate_before, sizeof(candidate)) == 0);

  muzzleflash.entity = 3;
  muzzleflash.weapon = WORR_EVENT_MONSTER_MUZZLE_LAST;
  muzzleflash.silenced = false;
  CHECK(SV_SnapshotShadowBuildMuzzleCandidateV1(
            peer, ref, max_entities, &muzzleflash,
            WORR_EVENT_MUZZLE_FAMILY_MONSTER, &candidate) ==
        SV_SNAPSHOT_EVENT_CANDIDATES_OK);
  std::memcpy(&payload, candidate.payload, sizeof(payload));
  CHECK(candidate.source_entity.index == 3 &&
        payload.family == WORR_EVENT_MUZZLE_FAMILY_MONSTER &&
        payload.flash_id == WORR_EVENT_MONSTER_MUZZLE_LAST &&
        payload.flags == 0);

  std::array<q2proto_svc_muzzleflash_t, 2> muzzle_batch{};
  std::array<uint32_t, 2> family_batch{
      WORR_EVENT_MUZZLE_FAMILY_PLAYER, WORR_EVENT_MUZZLE_FAMILY_MONSTER};
  std::array<worr_event_record_v1, 2> candidate_batch{};
  muzzle_batch[0].entity = 7;
  muzzle_batch[0].weapon = WORR_EVENT_PLAYER_MUZZLE_MACHINEGUN;
  muzzle_batch[0].silenced = true;
  muzzle_batch[1] = muzzleflash;
  CHECK(SV_SnapshotShadowBuildMuzzleCandidatesV1(
            peer, ref, max_entities, muzzle_batch.data(), family_batch.data(),
            static_cast<uint32_t>(muzzle_batch.size()), candidate_batch.data()) ==
        SV_SNAPSHOT_EVENT_CANDIDATES_OK);
  CHECK(candidate_batch[0].source_entity.index == 7 &&
        candidate_batch[1].source_entity.index == 3 &&
        Worr_EventRecordCandidateValidateV1(&candidate_batch[0], max_entities) &&
        Worr_EventRecordCandidateValidateV1(&candidate_batch[1], max_entities));

  const auto candidate_batch_before = candidate_batch;
  muzzle_batch[1].entity = 6;
  CHECK(SV_SnapshotShadowBuildMuzzleCandidatesV1(
            peer, ref, max_entities, muzzle_batch.data(), family_batch.data(),
            static_cast<uint32_t>(muzzle_batch.size()), candidate_batch.data()) ==
        SV_SNAPSHOT_EVENT_CANDIDATES_SOURCE_NOT_VISIBLE);
  CHECK(std::memcmp(candidate_batch.data(), candidate_batch_before.data(),
                    sizeof(candidate_batch)) == 0);
  CHECK(SV_SnapshotShadowBuildMuzzleCandidatesV1(
            peer, ref, max_entities, muzzle_batch.data(), family_batch.data(),
            17, candidate_batch.data()) == SV_SNAPSHOT_EVENT_CANDIDATES_CAPACITY);
  CHECK(std::memcmp(candidate_batch.data(), candidate_batch_before.data(),
                    sizeof(candidate_batch)) == 0);

  muzzleflash.silenced = true;
  const auto invalid_before = candidate;
  CHECK(SV_SnapshotShadowBuildMuzzleCandidateV1(
            peer, ref, max_entities, &muzzleflash,
            WORR_EVENT_MUZZLE_FAMILY_MONSTER, &candidate) ==
        SV_SNAPSHOT_EVENT_CANDIDATES_INVALID_CANDIDATE);
  CHECK(std::memcmp(&candidate, &invalid_before, sizeof(candidate)) == 0);

  SV_SnapshotShadowDestroyV1(peer);
}

void test_visible_temp_final_emission_binding() {
  const auto shadow_config = config();
  auto *peer = SV_SnapshotShadowCreateV1(&shadow_config);
  CHECK(peer != nullptr);

  auto frame = make_frame(43, -1, true);
  add_delta(frame, 3, entity_delta(3, 1));
  add_delta(frame, 7, entity_delta(7, 1));
  const auto ref = send(peer, frame, 2010, UINT64_C(61000000));

  q2proto_svc_temp_entity_t temp{};
  temp.type = WORR_EVENT_LEGACY_TEMP_GUNSHOT;
  temp.position1[0] = 12.0f;
  temp.direction[2] = 1.0f;

  worr_event_record_v1 candidate{};
  CHECK(SV_SnapshotShadowBuildTempCandidateV1(peer, ref, max_entities, &temp,
                                              &candidate) ==
        SV_SNAPSHOT_EVENT_CANDIDATES_OK);
  CHECK(Worr_EventRecordCandidateValidateV1(&candidate, max_entities));
  CHECK(candidate.source_tick == 43 &&
        candidate.source_time_us == UINT64_C(61000000) &&
        candidate.source_entity.index == 0 && candidate.source_entity.generation == 1 &&
        candidate.subject_entity.index == WORR_EVENT_NO_ENTITY &&
        candidate.subject_entity.generation == 0);
  worr_event_payload_legacy_temp_v1 payload{};
  std::memcpy(&payload, candidate.payload, sizeof(payload));
  CHECK(candidate.event_type == WORR_EVENT_TYPE_VISUAL_EFFECT &&
        payload.subtype == WORR_EVENT_LEGACY_TEMP_GUNSHOT &&
        payload.position1[0] == 12.0f && payload.direction[2] == 1.0f);

  temp = {};
  temp.type = WORR_EVENT_LEGACY_TEMP_LIGHTNING;
  temp.entity1 = 3;
  temp.entity2 = 7;
  temp.position1[0] = 1.0f;
  temp.position2[0] = 2.0f;
  CHECK(SV_SnapshotShadowBuildTempCandidateV1(peer, ref, max_entities, &temp,
                                              &candidate) ==
        SV_SNAPSHOT_EVENT_CANDIDATES_OK);
  CHECK(candidate.source_entity.index == 3 &&
        candidate.source_entity.generation == 1 &&
        candidate.subject_entity.index == 7 &&
        candidate.subject_entity.generation == 1);
  std::memcpy(&payload, candidate.payload, sizeof(payload));
  CHECK(payload.subtype == WORR_EVENT_LEGACY_TEMP_LIGHTNING &&
        payload.raw_entity1 == 3 && payload.raw_entity2 == 7);

  std::array<q2proto_svc_temp_entity_t, 2> temp_batch{};
  std::array<worr_event_record_v1, 2> candidate_batch{};
  temp_batch[0].type = WORR_EVENT_LEGACY_TEMP_GUNSHOT;
  temp_batch[0].position1[0] = 12.0f;
  temp_batch[0].direction[2] = 1.0f;
  temp_batch[1] = temp;
  CHECK(SV_SnapshotShadowBuildTempCandidatesV1(
            peer, ref, max_entities, temp_batch.data(),
            static_cast<uint32_t>(temp_batch.size()), candidate_batch.data()) ==
        SV_SNAPSHOT_EVENT_CANDIDATES_OK);
  CHECK(candidate_batch[0].source_entity.index == 0 &&
        candidate_batch[0].source_entity.generation == 1 &&
        candidate_batch[1].source_entity.index == 3 &&
        candidate_batch[1].subject_entity.index == 7 &&
        Worr_EventRecordCandidateValidateV1(&candidate_batch[0], max_entities) &&
        Worr_EventRecordCandidateValidateV1(&candidate_batch[1], max_entities));

  const auto candidate_batch_before = candidate_batch;
  temp_batch[1].entity2 = 2;
  CHECK(SV_SnapshotShadowBuildTempCandidatesV1(
            peer, ref, max_entities, temp_batch.data(),
            static_cast<uint32_t>(temp_batch.size()), candidate_batch.data()) ==
        SV_SNAPSHOT_EVENT_CANDIDATES_SOURCE_NOT_VISIBLE);
  CHECK(std::memcmp(candidate_batch.data(), candidate_batch_before.data(),
                    sizeof(candidate_batch)) == 0);
  CHECK(SV_SnapshotShadowBuildTempCandidatesV1(
            peer, ref, max_entities, temp_batch.data(), 17,
            candidate_batch.data()) == SV_SNAPSHOT_EVENT_CANDIDATES_CAPACITY);
  CHECK(std::memcmp(candidate_batch.data(), candidate_batch_before.data(),
                    sizeof(candidate_batch)) == 0);

  const auto candidate_before = candidate;
  temp.entity1 = 2;
  CHECK(SV_SnapshotShadowBuildTempCandidateV1(peer, ref, max_entities, &temp,
                                              &candidate) ==
        SV_SNAPSHOT_EVENT_CANDIDATES_SOURCE_NOT_VISIBLE);
  CHECK(std::memcmp(&candidate, &candidate_before, sizeof(candidate)) == 0);
  temp.entity1 = 3;
  temp.entity2 = 2;
  CHECK(SV_SnapshotShadowBuildTempCandidateV1(peer, ref, max_entities, &temp,
                                              &candidate) ==
        SV_SNAPSHOT_EVENT_CANDIDATES_SOURCE_NOT_VISIBLE);
  CHECK(std::memcmp(&candidate, &candidate_before, sizeof(candidate)) == 0);

  SV_SnapshotShadowDestroyV1(peer);
}

void test_visible_mixed_game_event_final_emission_binding() {
  const auto shadow_config = config();
  auto *peer = SV_SnapshotShadowCreateV1(&shadow_config);
  CHECK(peer != nullptr);

  auto frame = make_frame(47, -1, true);
  add_delta(frame, 3, entity_delta(3, 1));
  add_delta(frame, 7, entity_delta(7, 1));
  const auto ref = send(peer, frame, 2020, UINT64_C(62000000));

  std::array<worr_legacy_game_event_candidate_carrier_v1, 3> carriers{};
  std::array<worr_event_record_v1, 3> candidates{};
  carriers[0].kind = WORR_LEGACY_GAME_EVENT_CANDIDATE_TEMP_ENTITY;
  carriers[0].temp_entity.type = WORR_EVENT_LEGACY_TEMP_GUNSHOT;
  carriers[0].temp_entity.direction[2] = 1.0f;
  carriers[1].kind = WORR_LEGACY_GAME_EVENT_CANDIDATE_MUZZLEFLASH;
  carriers[1].muzzle_family = WORR_EVENT_MUZZLE_FAMILY_MONSTER;
  carriers[1].muzzleflash.entity = 3;
  carriers[1].muzzleflash.weapon = WORR_EVENT_MONSTER_MUZZLE_LAST;
  carriers[2].kind = WORR_LEGACY_GAME_EVENT_CANDIDATE_TEMP_ENTITY;
  carriers[2].temp_entity.type = WORR_EVENT_LEGACY_TEMP_LIGHTNING;
  carriers[2].temp_entity.entity1 = 3;
  carriers[2].temp_entity.entity2 = 7;

  CHECK(SV_SnapshotShadowBuildGameEventCandidatesV1(
            peer, ref, max_entities, carriers.data(),
            static_cast<uint32_t>(carriers.size()), candidates.data()) ==
        SV_SNAPSHOT_EVENT_CANDIDATES_OK);
  CHECK(candidates[0].source_entity.index == 0 &&
        candidates[1].source_entity.index == 3 &&
        candidates[2].source_entity.index == 3 &&
        candidates[2].subject_entity.index == 7 &&
        candidates[0].payload_kind == WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1 &&
        candidates[1].payload_kind == WORR_EVENT_PAYLOAD_MUZZLE_V1 &&
        candidates[2].payload_kind == WORR_EVENT_PAYLOAD_LEGACY_TEMP_V1);

  const auto candidates_before = candidates;
  carriers[2].temp_entity.entity2 = 2;
  CHECK(SV_SnapshotShadowBuildGameEventCandidatesV1(
            peer, ref, max_entities, carriers.data(),
            static_cast<uint32_t>(carriers.size()), candidates.data()) ==
        SV_SNAPSHOT_EVENT_CANDIDATES_SOURCE_NOT_VISIBLE);
  CHECK(std::memcmp(candidates.data(), candidates_before.data(),
                    sizeof(candidates)) == 0);
  CHECK(SV_SnapshotShadowBuildGameEventCandidatesV1(
            peer, ref, max_entities, carriers.data(), 17, candidates.data()) ==
        SV_SNAPSHOT_EVENT_CANDIDATES_CAPACITY);
  CHECK(std::memcmp(candidates.data(), candidates_before.data(),
                    sizeof(candidates)) == 0);

  SV_SnapshotShadowDestroyV1(peer);
}

} // namespace

int main() {
  test_canonical_constructor_catalog_and_atomic_failure();
  test_final_per_peer_retention_capacity_and_stale_refs();
  test_visible_spatial_audio_final_emission_binding();
  test_visible_muzzle_final_emission_binding();
  test_visible_temp_final_emission_binding();
  test_visible_mixed_game_event_final_emission_binding();
  std::puts("server_snapshot_event_candidates_test: ok");
  return 0;
}
