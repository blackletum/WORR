/* Public q2proto server -> wire -> public client -> Stage B oracle parity. */

#include "common/net/snapshot_q2proto.h"

#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace {

struct memory_io_t {
    std::array<uint8_t, 16384> data{};
    std::array<uint8_t, 16384> overflow{};
    size_t read_pos = 0;
    size_t write_pos = 0;
    bool failed = false;
    q2proto_error_t codec_error = Q2P_ERR_SUCCESS;

    void clear()
    {
        data.fill(0);
        read_pos = 0;
        write_pos = 0;
        failed = false;
        codec_error = Q2P_ERR_SUCCESS;
    }
};

memory_io_t &io_from(uintptr_t argument)
{
    return *reinterpret_cast<memory_io_t *>(argument);
}

[[noreturn]] void fail(const char *expression, int line)
{
    std::fprintf(stderr, "snapshot_q2proto_wire_test:%d: %s\n", line,
                 expression);
    std::exit(EXIT_FAILURE);
}

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression))                                                    \
            fail(#expression, __LINE__);                                      \
    } while (0)

const uint8_t *read_exact(memory_io_t &io, size_t size)
{
    if (size > io.write_pos - io.read_pos) {
        io.failed = true;
        return nullptr;
    }
    const uint8_t *result = io.data.data() + io.read_pos;
    io.read_pos += size;
    return result;
}

void *reserve_exact(memory_io_t &io, size_t size)
{
    if (size > io.data.size() - io.write_pos) {
        io.failed = true;
        return io.overflow.data();
    }
    void *result = io.data.data() + io.write_pos;
    io.write_pos += size;
    return result;
}

template <typename T>
T read_little(uintptr_t argument)
{
    const uint8_t *bytes = read_exact(io_from(argument), sizeof(T));
    if (bytes == nullptr)
        return std::numeric_limits<T>::max();
    uint64_t value = 0;
    for (size_t i = 0; i < sizeof(T); ++i)
        value |= static_cast<uint64_t>(bytes[i]) << (i * 8);
    return static_cast<T>(value);
}

template <typename T>
void write_little(uintptr_t argument, T value)
{
    auto *bytes = static_cast<uint8_t *>(
        reserve_exact(io_from(argument), sizeof(T)));
    for (size_t i = 0; i < sizeof(T); ++i)
        bytes[i] = static_cast<uint8_t>(value >> (i * 8));
}

struct protocol_fixture_t {
    q2proto_server_info_t info{};
    q2proto_connect_t connect{};
    q2proto_servercontext_t server{};
    q2proto_clientcontext_t client{};
    memory_io_t io{};
};

void initialize_protocol(protocol_fixture_t &fixture,
                         q2proto_protocol_t protocol,
                         q2proto_game_api_t game_api)
{
    fixture = {};
    fixture.info.game_api = game_api;
    fixture.info.default_packet_length = 1400;
    fixture.connect.protocol = protocol;
    fixture.connect.qport = 27901;
    fixture.connect.challenge = 42;
    fixture.connect.userinfo = q2proto_make_string("\\name\\snapshot");
    fixture.connect.packet_length = 1400;
    CHECK(q2proto_complete_connect(&fixture.connect) == Q2P_ERR_SUCCESS);
    CHECK(q2proto_init_servercontext(&fixture.server, &fixture.info,
                                      &fixture.connect) == Q2P_ERR_SUCCESS);
    CHECK(q2proto_init_clientcontext(&fixture.client) == Q2P_ERR_SUCCESS);

    q2proto_svc_message_t serverdata{};
    serverdata.type = Q2P_SVC_SERVERDATA;
    CHECK(q2proto_server_fill_serverdata(&fixture.server,
                                          &serverdata.serverdata) ==
          Q2P_ERR_SUCCESS);
    serverdata.serverdata.servercount = 1;
    serverdata.serverdata.gamedir = q2proto_make_string("basew");
    serverdata.serverdata.clientnum = 0;
    serverdata.serverdata.levelname = q2proto_make_string("snapshot wire");
    serverdata.serverdata.q2pro.server_state = 2;
    serverdata.serverdata.q2repro.server_fps = 40;
    CHECK(q2proto_server_write(
              &fixture.server, reinterpret_cast<uintptr_t>(&fixture.io),
              &serverdata) == Q2P_ERR_SUCCESS);
    q2proto_svc_message_t decoded{};
    CHECK(q2proto_client_read(
              &fixture.client, reinterpret_cast<uintptr_t>(&fixture.io),
              &decoded) == Q2P_ERR_SUCCESS);
    CHECK(decoded.type == Q2P_SVC_SERVERDATA && !fixture.io.failed);
}

struct oracle_fixture_t {
    worr_snapshot_q2proto_context_v2 context{};
    std::array<worr_snapshot_q2proto_slot_v2, 2> slots{};
    std::array<worr_snapshot_entity_v2, 16> entities{};
    std::array<uint8_t, 16> area{};
    std::array<worr_snapshot_event_ref_v2, 8> events{};
    std::array<worr_snapshot_q2proto_lineage_v2, 32> lineages{};
    std::array<worr_snapshot_entity_v2, 16> baselines{};
    std::array<uint8_t, 16> baseline_present{};
    std::array<worr_snapshot_entity_v2, 8> scratch_entities{};
    std::array<uint8_t, 8> scratch_area{};
    std::array<worr_snapshot_event_ref_v2, 4> scratch_events{};
    std::array<worr_snapshot_q2proto_lineage_v2, 16> scratch_lineage{};
};

void initialize_oracle(oracle_fixture_t &oracle, bool extended)
{
    oracle = {};
    worr_snapshot_q2proto_profile_v2 profile{};
    profile.struct_size = sizeof(profile);
    profile.schema_version = WORR_SNAPSHOT_Q2PROTO_VERSION;
    profile.snapshot_epoch = 5;
    profile.max_entities = 16;
    profile.max_models = 64;
    profile.max_sounds = 64;
    profile.beam_renderfx_mask = 1u << 7;
    profile.legacy_renderfx_allowed_mask = (1u << 19) - 1u;
    profile.legacy_beam_clear_mask = 1u << 9;
    profile.extended_entity_state = extended;
    worr_snapshot_q2proto_storage_v2 storage{};
    storage.struct_size = sizeof(storage);
    storage.schema_version = WORR_SNAPSHOT_Q2PROTO_VERSION;
    storage.slots = oracle.slots.data();
    storage.entities = oracle.entities.data();
    storage.area_bytes = oracle.area.data();
    storage.event_refs = oracle.events.data();
    storage.lineages = oracle.lineages.data();
    storage.baselines = oracle.baselines.data();
    storage.baseline_present = oracle.baseline_present.data();
    storage.scratch_entities = oracle.scratch_entities.data();
    storage.scratch_area_bytes = oracle.scratch_area.data();
    storage.scratch_event_refs = oracle.scratch_events.data();
    storage.scratch_lineage = oracle.scratch_lineage.data();
    storage.slot_capacity = 2;
    storage.entities_per_slot = 8;
    storage.area_bytes_per_slot = 8;
    storage.event_refs_per_slot = 4;
    storage.entity_storage_capacity = oracle.entities.size();
    storage.area_storage_capacity = oracle.area.size();
    storage.event_storage_capacity = oracle.events.size();
    storage.lineage_storage_capacity = oracle.lineages.size();
    storage.scratch_entity_capacity = oracle.scratch_entities.size();
    storage.scratch_area_capacity = oracle.scratch_area.size();
    storage.scratch_event_capacity = oracle.scratch_events.size();
    storage.scratch_lineage_capacity = oracle.scratch_lineage.size();
    CHECK(Worr_SnapshotQ2ProtoInitV2(&oracle.context, &profile, &storage) ==
          WORR_SNAPSHOT_Q2PROTO_OK);
}

uint64_t round_trip(q2proto_protocol_t protocol,
                    q2proto_game_api_t game_api)
{
    protocol_fixture_t fixture;
    initialize_protocol(fixture, protocol, game_api);
    fixture.io.clear();

    q2proto_packed_player_state_t packed_player{};
    packed_player.pm_gravity = 800;
    packed_player.fov = 100;
    packed_player.stats[2] = 77;
    q2proto_packed_entity_state_t packed_entity{};
    packed_entity.modelindex = 2;
    packed_entity.frame = 3;
    packed_entity.event = WORR_EVENT_LEGACY_ENTITY_FOOTSTEP;

    const std::array<uint8_t, 4> areabits{1, 2, 3, 4};
    q2proto_svc_message_t message{};
    message.type = Q2P_SVC_FRAME;
    message.frame.serverframe = 41;
    message.frame.deltaframe = -1;
    message.frame.areabits_len = areabits.size();
    message.frame.areabits = areabits.data();
    q2proto_server_make_player_state_delta(&fixture.server, nullptr,
                                            &packed_player,
                                            &message.frame.playerstate);
    CHECK(q2proto_server_write(
              &fixture.server, reinterpret_cast<uintptr_t>(&fixture.io),
              &message) == Q2P_ERR_SUCCESS);
    message = {};
    message.type = Q2P_SVC_FRAME_ENTITY_DELTA;
    message.frame_entity_delta.newnum = 2;
    q2proto_server_make_entity_state_delta(
        &fixture.server, nullptr, &packed_entity, false,
        &message.frame_entity_delta.entity_delta);
    CHECK(q2proto_server_write(
              &fixture.server, reinterpret_cast<uintptr_t>(&fixture.io),
              &message) == Q2P_ERR_SUCCESS);
    message = {};
    message.type = Q2P_SVC_FRAME_ENTITY_DELTA;
    CHECK(q2proto_server_write(
              &fixture.server, reinterpret_cast<uintptr_t>(&fixture.io),
              &message) == Q2P_ERR_SUCCESS);
    CHECK(!fixture.io.failed);

    fixture.io.read_pos = 0;
    q2proto_svc_message_t decoded{};
    CHECK(q2proto_client_read(
              &fixture.client, reinterpret_cast<uintptr_t>(&fixture.io),
              &decoded) == Q2P_ERR_SUCCESS);
    CHECK(decoded.type == Q2P_SVC_FRAME);
    q2proto_svc_frame_t decoded_frame = decoded.frame;
    std::array<q2proto_svc_frame_entity_delta_t, 3> deltas{};
    uint32_t count = 0;
    do {
        CHECK(count < deltas.size());
        CHECK(q2proto_client_read(
                  &fixture.client,
                  reinterpret_cast<uintptr_t>(&fixture.io),
                  &decoded) == Q2P_ERR_SUCCESS);
        CHECK(decoded.type == Q2P_SVC_FRAME_ENTITY_DELTA);
        deltas[count++] = decoded.frame_entity_delta;
    } while (decoded.frame_entity_delta.newnum != 0);
    CHECK(fixture.io.read_pos == fixture.io.write_pos && !fixture.io.failed);

    oracle_fixture_t oracle;
    initialize_oracle(oracle, game_api != Q2PROTO_GAME_VANILLA);
    worr_snapshot_q2proto_frame_input_v2 input{};
    input.struct_size = sizeof(input);
    input.schema_version = WORR_SNAPSHOT_Q2PROTO_VERSION;
    input.frame = &decoded_frame;
    input.entity_deltas = deltas.data();
    input.entity_delta_count = count;
    input.flags = WORR_SNAPSHOT_Q2PROTO_FRAME_OBSERVER_ATTACH;
    input.controlled_entity_index = 1;
    input.server_time_us = UINT64_C(1025000);
    worr_snapshot_ref_v2 ref{};
    CHECK(Worr_SnapshotQ2ProtoPublishV2(&oracle.context, &input, &ref) ==
          WORR_SNAPSHOT_Q2PROTO_OK);
    worr_snapshot_projection_view_v2 view{};
    worr_snapshot_projection_hashes_v2 hashes{};
    CHECK(Worr_SnapshotQ2ProtoViewV2(&oracle.context, ref, &view,
                                     &hashes) ==
          WORR_SNAPSHOT_Q2PROTO_OK);
    CHECK(view.entity_count == 1 && view.event_ref_count == 1);
    CHECK(view.entities[0].model_index[0] == 2 &&
          view.entities[0].frame == 3);
    CHECK(view.player->fov == 100.0f &&
          view.player->movement.gravity == 800 &&
          view.player->stats[2] == 77);
    return hashes.legacy_parity_hash;
}

} // namespace

extern "C" uint8_t q2protoio_read_u8(uintptr_t argument)
{
    return read_little<uint8_t>(argument);
}
extern "C" uint16_t q2protoio_read_u16(uintptr_t argument)
{
    return read_little<uint16_t>(argument);
}
extern "C" uint32_t q2protoio_read_u32(uintptr_t argument)
{
    return read_little<uint32_t>(argument);
}
extern "C" uint64_t q2protoio_read_u64(uintptr_t argument)
{
    return read_little<uint64_t>(argument);
}
extern "C" q2proto_string_t q2protoio_read_string(uintptr_t argument)
{
    auto &io = io_from(argument);
    q2proto_string_t result{};
    result.str = reinterpret_cast<const char *>(io.data.data() + io.read_pos);
    while (io.read_pos < io.write_pos) {
        if (io.data[io.read_pos++] == 0)
            return result;
        ++result.len;
    }
    io.failed = true;
    return {};
}
extern "C" const void *q2protoio_read_raw(uintptr_t argument, size_t size,
                                            size_t *readcount)
{
    auto &io = io_from(argument);
    if (readcount != nullptr) {
        const size_t available = io.write_pos - io.read_pos;
        const size_t amount = size < available ? size : available;
        const void *result = io.data.data() + io.read_pos;
        io.read_pos += amount;
        *readcount = amount;
        return result;
    }
    return read_exact(io, size);
}
extern "C" size_t q2protoio_read_available(uintptr_t argument)
{
    const auto &io = io_from(argument);
    return io.write_pos - io.read_pos;
}
extern "C" void q2protoio_write_u8(uintptr_t argument, uint8_t value)
{
    write_little(argument, value);
}
extern "C" void q2protoio_write_u16(uintptr_t argument, uint16_t value)
{
    write_little(argument, value);
}
extern "C" void q2protoio_write_u32(uintptr_t argument, uint32_t value)
{
    write_little(argument, value);
}
extern "C" void q2protoio_write_u64(uintptr_t argument, uint64_t value)
{
    write_little(argument, value);
}
extern "C" void *q2protoio_write_reserve_raw(uintptr_t argument, size_t size)
{
    return reserve_exact(io_from(argument), size);
}
extern "C" void q2protoio_write_raw(uintptr_t argument, const void *data,
                                      size_t size, size_t *written)
{
    auto &io = io_from(argument);
    const size_t available = io.data.size() - io.write_pos;
    const size_t amount = written != nullptr && size > available
                              ? available
                              : size;
    if (written == nullptr && size > available) {
        io.failed = true;
        return;
    }
    std::memcpy(reserve_exact(io, amount), data, amount);
    if (written != nullptr)
        *written = amount;
}
extern "C" size_t q2protoio_write_available(uintptr_t argument)
{
    const auto &io = io_from(argument);
    return io.data.size() - io.write_pos;
}
extern "C" q2proto_error_t q2protoerr_client_read(
    uintptr_t argument, q2proto_error_t error, const char *, ...)
{
    io_from(argument).codec_error = error;
    return error;
}
extern "C" q2proto_error_t q2protoerr_client_write(
    uintptr_t argument, q2proto_error_t error, const char *, ...)
{
    io_from(argument).codec_error = error;
    return error;
}
extern "C" q2proto_error_t q2protoerr_server_write(
    uintptr_t argument, q2proto_error_t error, const char *, ...)
{
    io_from(argument).codec_error = error;
    return error;
}
extern "C" q2proto_error_t q2protoerr_server_read(
    uintptr_t argument, q2proto_error_t error, const char *, ...)
{
    io_from(argument).codec_error = error;
    return error;
}

#if Q2PROTO_COMPRESSION_DEFLATE
extern "C" q2proto_error_t q2protoio_inflate_begin(
    uintptr_t argument, q2proto_inflate_deflate_header_mode_t,
    uintptr_t *inflate_argument)
{
    *inflate_argument = argument;
    return Q2P_ERR_NOT_IMPLEMENTED;
}
extern "C" q2proto_error_t q2protoio_inflate_data(uintptr_t, uintptr_t,
                                                     size_t)
{
    return Q2P_ERR_NOT_IMPLEMENTED;
}
extern "C" q2proto_error_t q2protoio_inflate_stream_ended(uintptr_t,
                                                            bool *ended)
{
    *ended = false;
    return Q2P_ERR_NOT_IMPLEMENTED;
}
extern "C" q2proto_error_t q2protoio_inflate_end(uintptr_t)
{
    return Q2P_ERR_NOT_IMPLEMENTED;
}
extern "C" q2proto_error_t q2protoio_deflate_begin(
    q2protoio_deflate_args_t *, size_t,
    q2proto_inflate_deflate_header_mode_t, uintptr_t *argument)
{
    *argument = 0;
    return Q2P_ERR_NOT_IMPLEMENTED;
}
extern "C" q2proto_error_t q2protoio_deflate_get_data(
    uintptr_t, size_t *in_size, const void **out, size_t *out_size)
{
    if (in_size != nullptr)
        *in_size = 0;
    *out = nullptr;
    *out_size = 0;
    return Q2P_ERR_NOT_IMPLEMENTED;
}
extern "C" q2proto_error_t q2protoio_deflate_end(uintptr_t)
{
    return Q2P_ERR_NOT_IMPLEMENTED;
}
#endif

int main()
{
    const uint64_t vanilla =
        round_trip(Q2P_PROTOCOL_VANILLA, Q2PROTO_GAME_VANILLA);
    const uint64_t r1q2 =
        round_trip(Q2P_PROTOCOL_R1Q2, Q2PROTO_GAME_VANILLA);
    const uint64_t q2pro = round_trip(Q2P_PROTOCOL_Q2PRO,
                                      Q2PROTO_GAME_Q2PRO_EXTENDED);
    const uint64_t q2repro = round_trip(Q2P_PROTOCOL_Q2REPRO,
                                        Q2PROTO_GAME_RERELEASE);
    CHECK(vanilla == r1q2 && vanilla == q2pro && vanilla == q2repro);
    std::printf("snapshot_q2proto_wire_test: ok parity=%016llx\n",
                static_cast<unsigned long long>(vanilla));
    return 0;
}
