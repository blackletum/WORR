/* Standalone FR-10-T06 Stage C final-emission shadow tests. */

#include "server/snapshot_shadow.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr uint32_t max_entities = 16;
constexpr uint32_t slot_count = 4;
constexpr uint32_t entities_per_slot = 8;
constexpr uint32_t area_per_slot = 8;

[[noreturn]] void fail(const char *expression, int line)
{
    std::fprintf(stderr, "server_snapshot_shadow_test:%d: %s\n", line,
                 expression);
    std::exit(EXIT_FAILURE);
}

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression))                                                    \
            fail(#expression, __LINE__);                                      \
    } while (0)

q2proto_entity_state_delta_t entity_delta(uint16_t model, uint16_t frame)
{
    q2proto_entity_state_delta_t delta{};
    delta.delta_bits = Q2P_ESD_MODELINDEX | Q2P_ESD_FRAME;
    delta.modelindex = model;
    delta.frame = frame;
    return delta;
}

void set_write_coords(q2proto_maybe_diff_coords_t &coords,
                      const float previous[3], const float current[3])
{
    q2proto_var_coords_set_float(&coords.write.prev, previous);
    q2proto_var_coords_set_float(&coords.write.current, current);
}

q2proto_svc_playerstate_t full_player()
{
    q2proto_svc_playerstate_t player{};
    player.delta_bits = Q2P_PSD_PM_GRAVITY | Q2P_PSD_FOV |
                        Q2P_PSD_PM_VIEWHEIGHT;
    player.pm_gravity = 800;
    player.pm_viewheight = 22;
    player.fov = 100;
    player.statbits = UINT64_C(1) << 2;
    player.stats[2] = 77;
    return player;
}

struct frame_carrier_t {
    q2proto_svc_frame_t frame{};
    std::array<q2proto_svc_frame_entity_delta_t, 12> deltas{};
    uint32_t count = 1;
    std::array<uint8_t, 4> area{1, 2, 3, 4};
};

frame_carrier_t make_frame(int wire_frame, int wire_base, bool full_ps)
{
    frame_carrier_t result{};
    result.frame.serverframe = wire_frame;
    result.frame.deltaframe = wire_base;
    result.frame.areabits_len = result.area.size();
    result.frame.areabits = result.area.data();
    if (full_ps)
        result.frame.playerstate = full_player();
    return result;
}

void add_delta(frame_carrier_t &frame, uint16_t entity,
               const q2proto_entity_state_delta_t &delta,
               bool remove = false)
{
    CHECK(frame.count < frame.deltas.size());
    auto &carrier = frame.deltas[frame.count - 1u];
    carrier.newnum = entity;
    carrier.remove = remove;
    carrier.entity_delta = delta;
    ++frame.count;
    frame.deltas[frame.count - 1u] = {};
}

sv_snapshot_shadow_config_v1 config()
{
    sv_snapshot_shadow_config_v1 result{};
    result.struct_size = sizeof(result);
    result.schema_version = SV_SNAPSHOT_SHADOW_VERSION;
    result.snapshot_epoch = 17;
    result.max_entities = max_entities;
    result.max_models = 64;
    result.max_sounds = 64;
    result.slot_capacity = slot_count;
    result.entities_per_slot = entities_per_slot;
    result.area_bytes_per_slot = area_per_slot;
    result.beam_renderfx_mask = UINT32_C(1) << 7;
    result.legacy_renderfx_allowed_mask = (UINT32_C(1) << 19) - 1u;
    result.legacy_beam_clear_mask = UINT32_C(1) << 9;
    result.extended_entity_state = 1;
    return result;
}

sv_snapshot_shadow_ref_v1 send(
    sv_snapshot_shadow_peer_v1 *peer, frame_carrier_t &carrier,
    uint32_t server_tick, uint32_t tick_delta, uint64_t server_time_us,
    bool truncated = false, bool fragment_stall = false)
{
    sv_snapshot_shadow_frame_v1 input{};
    input.struct_size = sizeof(input);
    input.schema_version = SV_SNAPSHOT_SHADOW_VERSION;
    input.wire_frame = &carrier.frame;
    input.authoritative_server_tick = server_tick;
    input.authoritative_tick_delta = tick_delta;
    input.authoritative_server_time_us = server_time_us;
    input.controlled_entity_index = 1;
    input.canonical_movement_type = 0;
    input.canonical_movement_flags = 0;
    input.team_id = 0;
    CHECK(SV_SnapshotShadowBeginFrameV1(peer, &input) ==
          SV_SNAPSHOT_SHADOW_OK);
    for (uint32_t index = 0; index < carrier.count; ++index) {
        CHECK(SV_SnapshotShadowCaptureEntityDeltaV1(
                  peer, &carrier.deltas[index]) ==
              SV_SNAPSHOT_SHADOW_OK);
    }
    if (truncated)
        SV_SnapshotShadowMarkTransportTruncatedV1(peer);
    if (fragment_stall)
        SV_SnapshotShadowMarkFragmentStallV1(peer);
    sv_snapshot_shadow_ref_v1 ref{91, 92};
    CHECK(SV_SnapshotShadowCommitFrameV1(peer, &ref) ==
          SV_SNAPSHOT_SHADOW_OK);
    return ref;
}

struct projector_fixture_t {
    worr_snapshot_q2proto_context_v2 context{};
    std::array<worr_snapshot_q2proto_slot_v2, slot_count> slots{};
    std::array<worr_snapshot_entity_v2,
               slot_count * entities_per_slot> entities{};
    std::array<uint8_t, slot_count * area_per_slot> areas{};
    std::array<worr_snapshot_event_ref_v2,
               slot_count * entities_per_slot> events{};
    std::array<worr_snapshot_q2proto_lineage_v2,
               slot_count * max_entities> lineages{};
    std::array<worr_snapshot_entity_v2, max_entities> baselines{};
    std::array<uint8_t, max_entities> baseline_present{};
    std::array<worr_snapshot_entity_v2, entities_per_slot> scratch_entities{};
    std::array<uint8_t, area_per_slot> scratch_area{};
    std::array<worr_snapshot_event_ref_v2,
               entities_per_slot> scratch_events{};
    std::array<worr_snapshot_q2proto_lineage_v2,
               max_entities> scratch_lineage{};
};

void init_projector(projector_fixture_t &fixture)
{
    fixture = {};
    const auto server_config = config();
    worr_snapshot_q2proto_profile_v2 profile{};
    profile.struct_size = sizeof(profile);
    profile.schema_version = WORR_SNAPSHOT_Q2PROTO_VERSION;
    profile.snapshot_epoch = server_config.snapshot_epoch;
    profile.max_entities = server_config.max_entities;
    profile.max_models = server_config.max_models;
    profile.max_sounds = server_config.max_sounds;
    profile.beam_renderfx_mask = server_config.beam_renderfx_mask;
    profile.legacy_renderfx_allowed_mask =
        server_config.legacy_renderfx_allowed_mask;
    profile.legacy_beam_clear_mask =
        server_config.legacy_beam_clear_mask;
    profile.extended_entity_state = server_config.extended_entity_state;

    worr_snapshot_q2proto_storage_v2 storage{};
    storage.struct_size = sizeof(storage);
    storage.schema_version = WORR_SNAPSHOT_Q2PROTO_VERSION;
    storage.slots = fixture.slots.data();
    storage.entities = fixture.entities.data();
    storage.area_bytes = fixture.areas.data();
    storage.event_refs = fixture.events.data();
    storage.lineages = fixture.lineages.data();
    storage.baselines = fixture.baselines.data();
    storage.baseline_present = fixture.baseline_present.data();
    storage.scratch_entities = fixture.scratch_entities.data();
    storage.scratch_area_bytes = fixture.scratch_area.data();
    storage.scratch_event_refs = fixture.scratch_events.data();
    storage.scratch_lineage = fixture.scratch_lineage.data();
    storage.slot_capacity = slot_count;
    storage.entities_per_slot = entities_per_slot;
    storage.area_bytes_per_slot = area_per_slot;
    storage.event_refs_per_slot = entities_per_slot;
    storage.entity_storage_capacity = fixture.entities.size();
    storage.area_storage_capacity = fixture.areas.size();
    storage.event_storage_capacity = fixture.events.size();
    storage.lineage_storage_capacity = fixture.lineages.size();
    storage.scratch_entity_capacity = fixture.scratch_entities.size();
    storage.scratch_area_capacity = fixture.scratch_area.size();
    storage.scratch_event_capacity = fixture.scratch_events.size();
    storage.scratch_lineage_capacity = fixture.scratch_lineage.size();
    CHECK(Worr_SnapshotQ2ProtoInitV2(&fixture.context, &profile, &storage) ==
          WORR_SNAPSHOT_Q2PROTO_OK);
}

worr_snapshot_projection_hashes_v2 project(
    projector_fixture_t &fixture, frame_carrier_t &carrier,
    uint64_t server_time_us, uint32_t flags)
{
    worr_snapshot_q2proto_frame_input_v2 input{};
    input.struct_size = sizeof(input);
    input.schema_version = WORR_SNAPSHOT_Q2PROTO_VERSION;
    input.frame = &carrier.frame;
    input.entity_deltas = carrier.deltas.data();
    input.entity_delta_count = carrier.count;
    input.flags = flags |
                  WORR_SNAPSHOT_Q2PROTO_FRAME_MOVEMENT_TYPE_VALID |
                  WORR_SNAPSHOT_Q2PROTO_FRAME_MOVEMENT_FLAGS_VALID |
                  WORR_SNAPSHOT_Q2PROTO_FRAME_TEAM_ID_VALID;
    input.controlled_entity_index = 1;
    input.server_time_us = server_time_us;
    worr_snapshot_ref_v2 ref{};
    CHECK(Worr_SnapshotQ2ProtoPublishV2(&fixture.context, &input, &ref) ==
          WORR_SNAPSHOT_Q2PROTO_OK);
    worr_snapshot_projection_view_v2 view{};
    worr_snapshot_projection_hashes_v2 hashes{};
    CHECK(Worr_SnapshotQ2ProtoViewV2(&fixture.context, ref, &view,
                                     &hashes) ==
          WORR_SNAPSHOT_Q2PROTO_OK);
    return hashes;
}

void test_final_emission_refs_and_parity()
{
    const auto server_config = config();
    auto *peer = SV_SnapshotShadowCreateV1(&server_config);
    CHECK(peer != nullptr);
    projector_fixture_t receiver;
    init_projector(receiver);

    const auto baseline = entity_delta(7, 1);
    CHECK(SV_SnapshotShadowSetBaselineV1(peer, 5, &baseline) ==
          SV_SNAPSHOT_SHADOW_OK);
    CHECK(Worr_SnapshotQ2ProtoSetBaselineV2(
              &receiver.context, 5, &baseline) ==
          WORR_SNAPSHOT_Q2PROTO_OK);

    auto frame10 = make_frame(10, -1, true);
    add_delta(frame10, 2, entity_delta(2, 1));
    auto from_baseline = entity_delta(0, 3);
    from_baseline.delta_bits = Q2P_ESD_FRAME;
    add_delta(frame10, 5, from_baseline);
    const auto ref10 = send(peer, frame10, 1000, 0, UINT64_C(25000000));
    const auto receiver10 = project(
        receiver, frame10, UINT64_C(25000000),
        WORR_SNAPSHOT_Q2PROTO_FRAME_OBSERVER_ATTACH);

    sv_snapshot_shadow_sent_v1 sent10{};
    CHECK(SV_SnapshotShadowGetSentV1(peer, ref10, &sent10) ==
          SV_SNAPSHOT_SHADOW_OK);
    CHECK(sent10.wire_snapshot_number == 10);
    CHECK(sent10.snapshot.snapshot_id.sequence == 11);
    CHECK(sent10.authoritative_server_tick == 1000);
    CHECK(sent10.snapshot.server_tick == 1000);
    CHECK(sent10.snapshot.server_tick != sent10.snapshot.snapshot_id.sequence);
    CHECK(sent10.base_ref.slot == SV_SNAPSHOT_SHADOW_NO_SLOT);
    CHECK(sent10.hashes.legacy_parity_hash ==
          receiver10.legacy_parity_hash);

    auto frame11 = make_frame(11, 10, false);
    const auto ref11 = send(
        peer, frame11, 1004, 4, UINT64_C(25100000), true, true);
    const auto receiver11 = project(
        receiver, frame11, UINT64_C(25100000),
        WORR_SNAPSHOT_Q2PROTO_FRAME_TRANSPORT_TRUNCATED |
            WORR_SNAPSHOT_Q2PROTO_FRAME_FRAGMENT_STALL);
    sv_snapshot_shadow_sent_v1 sent11{};
    CHECK(SV_SnapshotShadowGetSentV1(peer, ref11, &sent11) ==
          SV_SNAPSHOT_SHADOW_OK);
    CHECK(sent11.base_ref.slot == ref10.slot &&
          sent11.base_ref.generation == ref10.generation);
    CHECK(sent11.authoritative_tick_delta == 4);
    CHECK(sent11.snapshot.discontinuity.server_tick_delta == 4);
    CHECK((sent11.flags &
           SV_SNAPSHOT_SHADOW_SENT_TRANSPORT_TRUNCATED) != 0);
    CHECK((sent11.flags &
           SV_SNAPSHOT_SHADOW_SENT_FRAGMENT_STALL) != 0);
    CHECK((sent11.flags & SV_SNAPSHOT_SHADOW_SENT_RATE_SUPPRESSED) != 0);
    CHECK((sent11.snapshot.flags &
           WORR_SNAPSHOT_FLAG_TRANSPORT_TRUNCATED) != 0);
    CHECK((sent11.snapshot.flags &
           WORR_SNAPSHOT_FLAG_PROMOTION_ELIGIBLE) == 0);
    CHECK((sent11.snapshot.discontinuity.flags &
           WORR_SNAPSHOT_DISCONTINUITY_RATE_SUPPRESSED) != 0);
    CHECK((sent11.snapshot.discontinuity.flags &
           WORR_SNAPSHOT_DISCONTINUITY_FRAGMENT_STALL) != 0);
    CHECK(sent11.hashes.legacy_parity_hash ==
          receiver11.legacy_parity_hash);

    /* Exact base retention follows the acknowledged branch base, not the
     * most recently emitted frame. */
    auto frame12 = make_frame(12, 10, false);
    add_delta(frame12, 6, entity_delta(6, 1));
    const auto ref12 = send(
        peer, frame12, 1008, 4, UINT64_C(25200000));
    sv_snapshot_shadow_sent_v1 sent12{};
    CHECK(SV_SnapshotShadowGetSentV1(peer, ref12, &sent12) ==
          SV_SNAPSHOT_SHADOW_OK);
    CHECK(sent12.base_ref.slot == ref10.slot &&
          sent12.base_ref.generation == ref10.generation);
    CHECK((sent12.snapshot.discontinuity.flags &
           WORR_SNAPSHOT_DISCONTINUITY_BASE_JUMP) != 0);

    worr_snapshot_projection_view_v2 sent_view{};
    worr_snapshot_projection_hashes_v2 sent_hashes{};
    CHECK(SV_SnapshotShadowViewV1(
              peer, ref12, &sent_view, &sent_hashes) ==
          SV_SNAPSHOT_SHADOW_OK);
    CHECK(sent_view.snapshot->server_tick == 1008);
    CHECK(sent_view.entity_count == 3);
    CHECK(sent_hashes.endpoint_hash == sent12.hashes.endpoint_hash);

    SV_SnapshotShadowDestroyV1(peer);
}

void test_wire_zero_initial_and_zero_keyframe_base()
{
    const auto server_config = config();
    auto *peer = SV_SnapshotShadowCreateV1(&server_config);
    CHECK(peer != nullptr);

    auto frame0 = make_frame(0, -1, true);
    add_delta(frame0, 1, entity_delta(1, 1));
    const auto ref0 = send(peer, frame0, 0, 0, 0);
    sv_snapshot_shadow_sent_v1 sent0{};
    CHECK(SV_SnapshotShadowGetSentV1(peer, ref0, &sent0) ==
          SV_SNAPSHOT_SHADOW_OK);
    CHECK((sent0.snapshot.discontinuity.flags &
           WORR_SNAPSHOT_DISCONTINUITY_INITIAL) != 0);
    CHECK((sent0.snapshot.discontinuity.flags &
           WORR_SNAPSHOT_DISCONTINUITY_OBSERVER_ATTACH) == 0);

    /* Snapshot V2/q2proto defines delta frame zero as a keyframe marker. */
    auto frame1 = make_frame(1, 0, true);
    add_delta(frame1, 1, entity_delta(1, 2));
    const auto ref1 = send(peer, frame1, 1, 1, UINT64_C(25000));
    sv_snapshot_shadow_sent_v1 sent1{};
    CHECK(SV_SnapshotShadowGetSentV1(peer, ref1, &sent1) ==
          SV_SNAPSHOT_SHADOW_OK);
    CHECK(sent1.base_ref.slot == SV_SNAPSHOT_SHADOW_NO_SLOT);
    CHECK((sent1.snapshot.flags & WORR_SNAPSHOT_FLAG_KEYFRAME) != 0);
    CHECK((sent1.snapshot.discontinuity.flags &
           WORR_SNAPSHOT_DISCONTINUITY_FULL_SNAPSHOT) != 0);

    SV_SnapshotShadowDestroyV1(peer);
}

void test_full_entity_capacity_config()
{
    auto full_capacity = config();
    full_capacity.max_entities = full_capacity.entities_per_slot;
    auto *peer = SV_SnapshotShadowCreateV1(&full_capacity);
    CHECK(peer != nullptr);
    SV_SnapshotShadowDestroyV1(peer);

    full_capacity.max_entities = full_capacity.entities_per_slot - 1u;
    CHECK(SV_SnapshotShadowCreateV1(&full_capacity) == nullptr);
}

void test_server_write_coordinate_union_is_canonicalized()
{
    const auto server_config = config();
    auto *peer = SV_SnapshotShadowCreateV1(&server_config);
    CHECK(peer != nullptr);

    auto frame = make_frame(3, -1, true);
    const float player_previous[3] = {0.0f, 0.0f, 0.0f};
    const float player_current[3] = {32.0f, -16.0f, 8.0f};
    const float velocity_previous[3] = {1.0f, 2.0f, 3.0f};
    const float velocity_current[3] = {4.0f, 5.0f, 6.0f};
    set_write_coords(frame.frame.playerstate.pm_origin,
                     player_previous, player_current);
    set_write_coords(frame.frame.playerstate.pm_velocity,
                     velocity_previous, velocity_current);

    auto delta = entity_delta(2, 1);
    delta.delta_bits |= Q2P_ESD_LOOP_ATTENUATION;
    delta.loop_attenuation = 255;
    const float entity_previous[3] = {10.0f, 20.0f, 30.0f};
    const float entity_current[3] = {11.0f, 22.0f, 33.0f};
    set_write_coords(delta.origin, entity_previous, entity_current);
    add_delta(frame, 2, delta);
    const auto ref = send(peer, frame, 3, 0, UINT64_C(75000));

    worr_snapshot_projection_view_v2 projected{};
    worr_snapshot_projection_hashes_v2 hashes{};
    CHECK(SV_SnapshotShadowViewV1(peer, ref, &projected, &hashes) ==
          SV_SNAPSHOT_SHADOW_OK);
    CHECK(projected.player->movement.origin[0] == 32.0f);
    CHECK(projected.player->movement.origin[1] == -16.0f);
    CHECK(projected.player->movement.origin[2] == 8.0f);
    CHECK(projected.player->movement.velocity[0] == 4.0f);
    CHECK(projected.player->movement.velocity[1] == 5.0f);
    CHECK(projected.player->movement.velocity[2] == 6.0f);
    CHECK(projected.entity_count == 1);
    CHECK(projected.entities[0].origin[0] == 11.0f);
    CHECK(projected.entities[0].origin[1] == 22.0f);
    CHECK(projected.entities[0].origin[2] == 33.0f);
    CHECK(projected.entities[0].loop_attenuation == 0.0f);
    CHECK(hashes.endpoint_hash != 0);

    SV_SnapshotShadowDestroyV1(peer);
}

void test_abort_failure_and_stale_ref()
{
    const auto server_config = config();
    auto *peer = SV_SnapshotShadowCreateV1(&server_config);
    CHECK(peer != nullptr);

    auto frame20 = make_frame(20, -1, true);
    add_delta(frame20, 2, entity_delta(2, 1));
    const auto ref20 = send(peer, frame20, 200, 0, UINT64_C(5000000));

    auto frame21 = make_frame(21, 20, false);
    sv_snapshot_shadow_frame_v1 input{};
    input.struct_size = sizeof(input);
    input.schema_version = SV_SNAPSHOT_SHADOW_VERSION;
    input.wire_frame = &frame21.frame;
    input.authoritative_server_tick = 201;
    input.authoritative_tick_delta = 1;
    input.authoritative_server_time_us = UINT64_C(5025000);
    input.controlled_entity_index = 1;
    CHECK(SV_SnapshotShadowBeginFrameV1(peer, &input) ==
          SV_SNAPSHOT_SHADOW_OK);
    CHECK(SV_SnapshotShadowCaptureEntityDeltaV1(
              peer, &frame21.deltas[0]) == SV_SNAPSHOT_SHADOW_OK);
    SV_SnapshotShadowAbortFrameV1(peer);
    sv_snapshot_shadow_ref_v1 missing{};
    CHECK(SV_SnapshotShadowFindWireV1(peer, 21, &missing) ==
          SV_SNAPSHOT_SHADOW_STALE_REF);

    /* A packet missing its final entity sentinel cannot publish a partial
     * canonical endpoint. */
    auto malformed = make_frame(21, 20, false);
    add_delta(malformed, 3, entity_delta(3, 1));
    CHECK(SV_SnapshotShadowBeginFrameV1(peer, &input) ==
          SV_SNAPSHOT_SHADOW_OK);
    CHECK(SV_SnapshotShadowCaptureEntityDeltaV1(
              peer, &malformed.deltas[0]) == SV_SNAPSHOT_SHADOW_OK);
    sv_snapshot_shadow_ref_v1 sentinel{77, 88};
    CHECK(SV_SnapshotShadowCommitFrameV1(peer, &sentinel) ==
          SV_SNAPSHOT_SHADOW_CAPTURE_FAILED);
    CHECK(sentinel.slot == 77 && sentinel.generation == 88);
    CHECK(SV_SnapshotShadowFindWireV1(peer, 21, &missing) ==
          SV_SNAPSHOT_SHADOW_STALE_REF);

    /* Four newer successful emissions overwrite the four-slot exact-ref
     * window; generation validation then rejects the old process-local ref. */
    auto valid21 = make_frame(21, 20, false);
    const auto ref21 = send(peer, valid21, 201, 1, UINT64_C(5025000));
    auto frame22 = make_frame(22, 21, false);
    const auto ref22 = send(peer, frame22, 202, 1, UINT64_C(5050000));
    auto frame23 = make_frame(23, 22, false);
    const auto ref23 = send(peer, frame23, 203, 1, UINT64_C(5075000));
    auto frame24 = make_frame(24, 23, false);
    (void)send(peer, frame24, 204, 1, UINT64_C(5100000));
    (void)ref21;
    (void)ref22;
    (void)ref23;
    sv_snapshot_shadow_sent_v1 stale{};
    CHECK(SV_SnapshotShadowGetSentV1(peer, ref20, &stale) ==
          SV_SNAPSHOT_SHADOW_STALE_REF);

    sv_snapshot_shadow_status_v1 status{};
    CHECK(SV_SnapshotShadowGetStatusV1(peer, &status));
    CHECK(status.frames_committed == 5);
    CHECK(status.pending_aborts >= 2);
    CHECK(status.retained_count == slot_count);
    CHECK(status.stale_ref_queries >= 3);
    CHECK(status.last_endpoint_hash != 0);
    CHECK(status.last_legacy_parity_hash != 0);
    CHECK(status.allocation_bytes != 0);

    SV_SnapshotShadowDestroyV1(peer);
}

} // namespace

int main()
{
    test_final_emission_refs_and_parity();
    test_full_entity_capacity_config();
    test_server_write_coordinate_union_is_canonicalized();
    test_wire_zero_initial_and_zero_keyframe_base();
    test_abort_failure_and_stale_ref();
    std::puts("server_snapshot_shadow_test: ok");
    return 0;
}
