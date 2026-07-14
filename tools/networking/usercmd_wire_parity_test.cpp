/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/usercmd_delta.h"
#include "shared/prediction_abi.h"

#include <array>
#include <bit>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

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

struct wire_evidence_t {
    std::string protocol;
    std::string message;
    bool has_upmove = false;
    size_t wire_bytes = 0;
    std::string wire_sha256;
    std::vector<usercmd_t> canonical;
    std::vector<usercmd_t> decoded;
    bool canonical_idempotent = false;
    bool decoded_equal = false;
};

struct sha256_t {
    std::array<uint32_t, 8> state{
        UINT32_C(0x6a09e667), UINT32_C(0xbb67ae85),
        UINT32_C(0x3c6ef372), UINT32_C(0xa54ff53a),
        UINT32_C(0x510e527f), UINT32_C(0x9b05688c),
        UINT32_C(0x1f83d9ab), UINT32_C(0x5be0cd19),
    };

    static uint32_t rotate_right(uint32_t value, unsigned amount)
    {
        return (value >> amount) | (value << (32 - amount));
    }

    void block(const uint8_t *bytes)
    {
        static constexpr std::array<uint32_t, 64> constants{
            UINT32_C(0x428a2f98), UINT32_C(0x71374491),
            UINT32_C(0xb5c0fbcf), UINT32_C(0xe9b5dba5),
            UINT32_C(0x3956c25b), UINT32_C(0x59f111f1),
            UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5),
            UINT32_C(0xd807aa98), UINT32_C(0x12835b01),
            UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
            UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe),
            UINT32_C(0x9bdc06a7), UINT32_C(0xc19bf174),
            UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786),
            UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc),
            UINT32_C(0x2de92c6f), UINT32_C(0x4a7484aa),
            UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da),
            UINT32_C(0x983e5152), UINT32_C(0xa831c66d),
            UINT32_C(0xb00327c8), UINT32_C(0xbf597fc7),
            UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147),
            UINT32_C(0x06ca6351), UINT32_C(0x14292967),
            UINT32_C(0x27b70a85), UINT32_C(0x2e1b2138),
            UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
            UINT32_C(0x650a7354), UINT32_C(0x766a0abb),
            UINT32_C(0x81c2c92e), UINT32_C(0x92722c85),
            UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b),
            UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3),
            UINT32_C(0xd192e819), UINT32_C(0xd6990624),
            UINT32_C(0xf40e3585), UINT32_C(0x106aa070),
            UINT32_C(0x19a4c116), UINT32_C(0x1e376c08),
            UINT32_C(0x2748774c), UINT32_C(0x34b0bcb5),
            UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a),
            UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3),
            UINT32_C(0x748f82ee), UINT32_C(0x78a5636f),
            UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
            UINT32_C(0x90befffa), UINT32_C(0xa4506ceb),
            UINT32_C(0xbef9a3f7), UINT32_C(0xc67178f2),
        };
        std::array<uint32_t, 64> words{};
        for (size_t i = 0; i < 16; ++i) {
            words[i] = (static_cast<uint32_t>(bytes[i * 4]) << 24) |
                       (static_cast<uint32_t>(bytes[i * 4 + 1]) << 16) |
                       (static_cast<uint32_t>(bytes[i * 4 + 2]) << 8) |
                       static_cast<uint32_t>(bytes[i * 4 + 3]);
        }
        for (size_t i = 16; i < words.size(); ++i) {
            const uint32_t s0 = rotate_right(words[i - 15], 7) ^
                                rotate_right(words[i - 15], 18) ^
                                (words[i - 15] >> 3);
            const uint32_t s1 = rotate_right(words[i - 2], 17) ^
                                rotate_right(words[i - 2], 19) ^
                                (words[i - 2] >> 10);
            words[i] = words[i - 16] + s0 + words[i - 7] + s1;
        }

        uint32_t a = state[0];
        uint32_t b = state[1];
        uint32_t c = state[2];
        uint32_t d = state[3];
        uint32_t e = state[4];
        uint32_t f = state[5];
        uint32_t g = state[6];
        uint32_t h = state[7];
        for (size_t i = 0; i < words.size(); ++i) {
            const uint32_t sum1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^
                                  rotate_right(e, 25);
            const uint32_t choice = (e & f) ^ (~e & g);
            const uint32_t temp1 = h + sum1 + choice + constants[i] + words[i];
            const uint32_t sum0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^
                                  rotate_right(a, 22);
            const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = sum0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }
};

std::string sha256_hex(const uint8_t *data, size_t size)
{
    std::vector<uint8_t> padded;
    padded.reserve(((size + 9 + 63) / 64) * 64);
    if (size)
        padded.insert(padded.end(), data, data + size);
    padded.push_back(0x80);
    while ((padded.size() % 64) != 56)
        padded.push_back(0);
    const uint64_t bit_size = static_cast<uint64_t>(size) * 8;
    for (int shift = 56; shift >= 0; shift -= 8)
        padded.push_back(static_cast<uint8_t>(bit_size >> shift));

    sha256_t hash{};
    for (size_t offset = 0; offset < padded.size(); offset += 64)
        hash.block(padded.data() + offset);

    static constexpr char digits[] = "0123456789abcdef";
    std::string result(64, '0');
    size_t output = 0;
    for (uint32_t word : hash.state) {
        for (int shift = 28; shift >= 0; shift -= 4)
            result[output++] = digits[(word >> shift) & 0xf];
    }
    return result;
}

memory_io_t &io_from(uintptr_t io_arg)
{
    return *reinterpret_cast<memory_io_t *>(io_arg);
}

[[noreturn]] void fail(const char *message)
{
    std::fprintf(stderr, "usercmd wire parity test failed: %s\n", message);
    std::exit(EXIT_FAILURE);
}

void require(bool condition, const char *message)
{
    if (!condition)
        fail(message);
}

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
T read_little(uintptr_t io_arg)
{
    memory_io_t &io = io_from(io_arg);
    const uint8_t *bytes = read_exact(io, sizeof(T));
    if (!bytes)
        return std::numeric_limits<T>::max();
    uint64_t value = 0;
    for (size_t i = 0; i < sizeof(T); ++i)
        value |= static_cast<uint64_t>(bytes[i]) << (i * 8);
    return static_cast<T>(value);
}

template <typename T>
void write_little(uintptr_t io_arg, T value)
{
    memory_io_t &io = io_from(io_arg);
    auto *bytes = static_cast<uint8_t *>(reserve_exact(io, sizeof(T)));
    for (size_t i = 0; i < sizeof(T); ++i)
        bytes[i] = static_cast<uint8_t>(value >> (i * 8));
}

struct protocol_fixture_t {
    const char *name = nullptr;
    bool batch = false;
    bool has_upmove = false;
    q2proto_server_info_t server_info{};
    q2proto_connect_t connect{};
    q2proto_servercontext_t server{};
    q2proto_clientcontext_t client{};
    memory_io_t io{};
};

template <size_t Count>
struct round_trip_result_t {
    std::array<usercmd_t, Count> canonical{};
    std::array<usercmd_t, Count> decoded{};
    size_t wire_bytes = 0;
    std::string wire_sha256;
    bool canonical_idempotent = false;
};

const char *protocol_name(q2proto_protocol_t protocol)
{
    switch (protocol) {
    case Q2P_PROTOCOL_VANILLA:
        return "vanilla";
    case Q2P_PROTOCOL_Q2PRO:
        return "q2pro";
    case Q2P_PROTOCOL_Q2REPRO:
        return "q2repro";
    default:
        return "unexpected";
    }
}

worr_prediction_command_v1 prediction_command(const usercmd_t &command)
{
    worr_prediction_command_v1 prediction{};
    require(NetUsercmd_ToPredictionCommandV1(&command, &prediction),
            "decoded command was not valid prediction input");
    return prediction;
}

bool equal_float(float left, float right)
{
    return std::bit_cast<uint32_t>(left) == std::bit_cast<uint32_t>(right);
}

bool command_fields_equal(const usercmd_t &left, const usercmd_t &right)
{
    if (left.msec != right.msec || left.buttons != right.buttons ||
        !equal_float(left.forwardmove, right.forwardmove) ||
        !equal_float(left.sidemove, right.sidemove)) {
        return false;
    }
    for (unsigned i = 0; i < 3; ++i) {
        if (!equal_float(left.angles[i], right.angles[i]))
            return false;
    }
    return true;
}

void require_command_equal(const usercmd_t &expected,
                           const usercmd_t &decoded)
{
    require(command_fields_equal(expected, decoded),
            "decoded command scalar fields diverged");

    const worr_prediction_command_v1 expected_prediction =
        prediction_command(expected);
    const worr_prediction_command_v1 decoded_prediction =
        prediction_command(decoded);
    require(Worr_PredictionHashCommandV1(&expected_prediction) ==
                Worr_PredictionHashCommandV1(&decoded_prediction),
            "decoded and stored prediction command hashes diverged");
}

template <size_t Count>
bool canonical_commands_are_idempotent(
    const std::array<usercmd_t, Count> &commands, bool has_upmove)
{
    for (const usercmd_t &command : commands) {
        usercmd_t second = command;
        if (!NetUsercmd_CanonicalizeForTransport(&second, has_upmove) ||
            !command_fields_equal(command, second)) {
            return false;
        }
    }
    return true;
}

usercmd_t make_command(uint8_t msec, float pitch, float yaw, float roll,
                       float forward, float side, uint8_t buttons)
{
    usercmd_t command{};
    command.msec = msec;
    command.angles[0] = pitch;
    command.angles[1] = yaw;
    command.angles[2] = roll;
    command.forwardmove = forward;
    command.sidemove = side;
    command.buttons = buttons;
    return command;
}

void initialize_protocol(protocol_fixture_t &fixture, const char *name,
                         q2proto_protocol_t protocol,
                         q2proto_game_api_t game_api, bool batch)
{
    fixture = {};
    fixture.name = name;
    fixture.batch = batch;
    fixture.server_info.game_api = game_api;
    fixture.server_info.default_packet_length = 1400;
    fixture.connect.protocol = protocol;
    fixture.connect.qport = 27901;
    fixture.connect.challenge = 12345;
    fixture.connect.userinfo = q2proto_make_string("\\name\\wiretest");
    fixture.connect.packet_length = 1400;

    require(q2proto_complete_connect(&fixture.connect) == Q2P_ERR_SUCCESS,
            "q2proto connect completion failed");
    require(q2proto_init_servercontext(&fixture.server,
                                       &fixture.server_info,
                                       &fixture.connect) == Q2P_ERR_SUCCESS,
            "q2proto server context initialization failed");
    require(q2proto_init_clientcontext(&fixture.client) == Q2P_ERR_SUCCESS,
            "q2proto client context initialization failed");

    q2proto_svc_message_t serverdata{};
    serverdata.type = Q2P_SVC_SERVERDATA;
    require(q2proto_server_fill_serverdata(&fixture.server,
                                           &serverdata.serverdata) ==
                Q2P_ERR_SUCCESS,
            "q2proto serverdata fill failed");
    serverdata.serverdata.servercount = 7;
    serverdata.serverdata.attractloop = false;
    serverdata.serverdata.gamedir = q2proto_make_string("basew");
    serverdata.serverdata.clientnum = 0;
    serverdata.serverdata.levelname = q2proto_make_string("wire parity");
    serverdata.serverdata.q2pro.server_state = 2;
    serverdata.serverdata.q2repro.server_fps = 40;

    fixture.io.clear();
    require(q2proto_server_write(
                &fixture.server, reinterpret_cast<uintptr_t>(&fixture.io),
                &serverdata) == Q2P_ERR_SUCCESS &&
                !fixture.io.failed,
            "q2proto serverdata serialization failed");
    fixture.io.read_pos = 0;
    q2proto_svc_message_t parsed{};
    require(q2proto_client_read(
                &fixture.client, reinterpret_cast<uintptr_t>(&fixture.io),
                &parsed) == Q2P_ERR_SUCCESS &&
                parsed.type == Q2P_SVC_SERVERDATA && !fixture.io.failed,
            "q2proto client serverdata parser failed");
    require(fixture.io.read_pos == fixture.io.write_pos,
            "serverdata parser left unread bytes");
    fixture.has_upmove = fixture.client.features.has_upmove;
    require(!batch || fixture.client.features.batch_move,
            "negotiated protocol lacks requested batch moves");
    require(fixture.has_upmove ==
                (game_api != Q2PROTO_GAME_RERELEASE),
            "negotiated upmove semantics do not match game API");
}

std::array<usercmd_t, 3> default_commands(bool has_upmove)
{
    std::array<usercmd_t, 3> commands{
        make_command(1, 3590.568115234375f, -180.0f, 90.0f, -512.0f,
                     511.0f, BUTTON_ATTACK | BUTTON_JUMP),
        make_command(66, 3590.568115234375f, -180.0f, 90.0f, -512.0f,
                     511.0f, BUTTON_ATTACK | BUTTON_JUMP),
        make_command(250, 999990.0625f, 720.25f, -450.5f, 511.0f,
                     -512.0f,
                     BUTTON_USE | BUTTON_CROUCH | BUTTON_HOLSTER |
                         BUTTON_ANY),
    };
    for (usercmd_t &command : commands) {
        require(NetUsercmd_CanonicalizeForTransport(&command, has_upmove),
                "default command canonicalization failed");
    }
    return commands;
}

std::array<usercmd_t, 8> batch_commands(bool has_upmove)
{
    std::array<usercmd_t, 8> commands{
        make_command(1, 3590.568115234375f, -180.0f, 90.0f, -512.0f,
                     511.0f, BUTTON_ATTACK | BUTTON_JUMP),
        make_command(66, 3590.568115234375f, -180.0f, 90.0f, -512.0f,
                     511.0f, BUTTON_ATTACK | BUTTON_JUMP),
        make_command(250, 999990.0625f, 720.25f, -450.5f, 511.0f,
                     -512.0f,
                     BUTTON_USE | BUTTON_CROUCH | BUTTON_HOLSTER |
                         BUTTON_ANY),
        make_command(0, 999990.0625f, 0.0f, 0.0f, 511.0f, -512.0f,
                     BUTTON_CROUCH),
        make_command(7, -999990.0625f, 45.0f, 0.0f, 0.0f, 0.0f,
                     BUTTON_HOLSTER | BUTTON_ATTACK),
        make_command(8, -999990.0625f, 45.0f, 0.0f, 0.0f, 0.0f,
                     BUTTON_JUMP | BUTTON_CROUCH),
        make_command(9, 180.0f, 270.0f, 360.0f, 1.0f, -1.0f,
                     BUTTON_ANY),
        make_command(255, 180.0f, 270.0f, 360.0f, 1.0f, -1.0f,
                     BUTTON_ANY),
    };
    for (usercmd_t &command : commands) {
        require(NetUsercmd_CanonicalizeForTransport(&command, has_upmove),
                "batch command canonicalization failed");
    }
    return commands;
}

round_trip_result_t<3> round_trip_default(protocol_fixture_t &fixture)
{
    round_trip_result_t<3> result{};
    result.canonical = default_commands(fixture.has_upmove);
    result.canonical_idempotent = canonical_commands_are_idempotent(
        result.canonical, fixture.has_upmove);
    require(result.canonical_idempotent,
            "default canonical command was not idempotent");
    q2proto_clc_message_t outgoing{};
    outgoing.type = Q2P_CLC_MOVE;
    outgoing.move.lastframe = -1;
    outgoing.move.sequence = INT32_MAX - 1;
    const usercmd_t *from = nullptr;
    for (size_t i = 0; i < result.canonical.size(); ++i) {
        require(NetUsercmd_BuildDelta(&outgoing.move.moves[i], from,
                                      &result.canonical[i], 47,
                                      fixture.has_upmove),
                "default command delta build failed");
        from = &result.canonical[i];
    }
    require((outgoing.move.moves[1].delta_bits &
             (Q2P_CMD_ANGLE0 | Q2P_CMD_ANGLE1 | Q2P_CMD_ANGLE2 |
              Q2P_CMD_MOVE_FORWARD | Q2P_CMD_MOVE_SIDE |
              Q2P_CMD_MOVE_UP | Q2P_CMD_BUTTONS)) == 0,
            "default delta inheritance emitted unchanged fields");

    fixture.io.clear();
    const q2proto_error_t write_error = q2proto_client_write(
        &fixture.client, reinterpret_cast<uintptr_t>(&fixture.io), &outgoing);
    if (write_error != Q2P_ERR_SUCCESS || fixture.io.failed) {
        std::fprintf(stderr,
                     "%s default writer error=%d feedback=%d failed=%d "
                     "bytes=%zu\n",
                     fixture.name, static_cast<int>(write_error),
                     static_cast<int>(fixture.io.codec_error),
                     fixture.io.failed ? 1 : 0, fixture.io.write_pos);
        fail("q2proto default client writer failed");
    }
    result.wire_bytes = fixture.io.write_pos;
    result.wire_sha256 =
        sha256_hex(fixture.io.data.data(), fixture.io.write_pos);
    fixture.io.read_pos = 0;
    q2proto_clc_message_t incoming{};
    require(q2proto_server_read(
                &fixture.server, reinterpret_cast<uintptr_t>(&fixture.io),
                &incoming) == Q2P_ERR_SUCCESS &&
                incoming.type == Q2P_CLC_MOVE && !fixture.io.failed,
            "q2proto default server reader failed");
    require(fixture.io.read_pos == fixture.io.write_pos,
            "default server reader left unread bytes");

    const usercmd_t *decoded_from = nullptr;
    for (size_t i = 0; i < result.decoded.size(); ++i) {
        require(NetUsercmd_ApplyDelta(&incoming.move.moves[i], decoded_from,
                                      &result.decoded[i], fixture.has_upmove),
                "default command delta apply failed");
        require_command_equal(result.canonical[i], result.decoded[i]);
        decoded_from = &result.decoded[i];
    }
    return result;
}

round_trip_result_t<8> round_trip_batch(protocol_fixture_t &fixture)
{
    round_trip_result_t<8> result{};
    result.canonical = batch_commands(fixture.has_upmove);
    result.canonical_idempotent = canonical_commands_are_idempotent(
        result.canonical, fixture.has_upmove);
    require(result.canonical_idempotent,
            "batch canonical command was not idempotent");
    q2proto_clc_message_t outgoing{};
    outgoing.type = Q2P_CLC_BATCH_MOVE;
    outgoing.batch_move.lastframe = 123;
    outgoing.batch_move.num_dups = 0;
    q2proto_clc_batch_move_frame_t &frame =
        outgoing.batch_move.batch_frames[0];
    frame.num_cmds = static_cast<uint8_t>(result.canonical.size());
    const usercmd_t *from = nullptr;
    for (size_t i = 0; i < result.canonical.size(); ++i) {
        require(NetUsercmd_BuildDelta(&frame.moves[i], from,
                                      &result.canonical[i],
                                      static_cast<uint8_t>(40 + i),
                                      fixture.has_upmove),
                "batch command delta build failed");
        from = &result.canonical[i];
    }
    require((frame.moves[1].delta_bits &
             (Q2P_CMD_ANGLE0 | Q2P_CMD_ANGLE1 | Q2P_CMD_ANGLE2 |
              Q2P_CMD_MOVE_FORWARD | Q2P_CMD_MOVE_SIDE |
              Q2P_CMD_MOVE_UP | Q2P_CMD_BUTTONS)) == 0,
            "batch delta inheritance emitted unchanged fields");

    fixture.io.clear();
    const q2proto_error_t write_error = q2proto_client_write(
        &fixture.client, reinterpret_cast<uintptr_t>(&fixture.io), &outgoing);
    if (write_error != Q2P_ERR_SUCCESS || fixture.io.failed) {
        std::fprintf(stderr,
                     "%s batch writer error=%d feedback=%d failed=%d "
                     "bytes=%zu\n",
                     fixture.name, static_cast<int>(write_error),
                     static_cast<int>(fixture.io.codec_error),
                     fixture.io.failed ? 1 : 0, fixture.io.write_pos);
        fail("q2proto batch client writer failed");
    }
    result.wire_bytes = fixture.io.write_pos;
    result.wire_sha256 =
        sha256_hex(fixture.io.data.data(), fixture.io.write_pos);
    fixture.io.read_pos = 0;
    q2proto_clc_message_t incoming{};
    require(q2proto_server_read(
                &fixture.server, reinterpret_cast<uintptr_t>(&fixture.io),
                &incoming) == Q2P_ERR_SUCCESS &&
                incoming.type == Q2P_CLC_BATCH_MOVE && !fixture.io.failed,
            "q2proto batch server reader failed");
    require(fixture.io.read_pos == fixture.io.write_pos,
            "batch server reader left unread bytes");
    require(incoming.batch_move.num_dups == 0 &&
                incoming.batch_move.batch_frames[0].num_cmds ==
                    result.canonical.size(),
            "decoded batch shape diverged");

    const q2proto_clc_batch_move_frame_t &decoded_frame =
        incoming.batch_move.batch_frames[0];
    const usercmd_t *decoded_from = nullptr;
    for (size_t i = 0; i < result.decoded.size(); ++i) {
        require(NetUsercmd_ApplyDelta(&decoded_frame.moves[i], decoded_from,
                                      &result.decoded[i], fixture.has_upmove),
                "batch command delta apply failed");
        require_command_equal(result.canonical[i], result.decoded[i]);
        decoded_from = &result.decoded[i];
    }
    return result;
}

template <size_t Count>
wire_evidence_t make_evidence(
    const protocol_fixture_t &fixture,
    const round_trip_result_t<Count> &result)
{
    wire_evidence_t evidence{};
    evidence.protocol = protocol_name(fixture.connect.protocol);
    evidence.message = fixture.batch ? "batch_move" : "move";
    evidence.has_upmove = fixture.has_upmove;
    evidence.wire_bytes = result.wire_bytes;
    evidence.wire_sha256 = result.wire_sha256;
    evidence.canonical.assign(result.canonical.begin(),
                              result.canonical.end());
    evidence.decoded.assign(result.decoded.begin(), result.decoded.end());
    evidence.canonical_idempotent = result.canonical_idempotent;
    evidence.decoded_equal = true;
    for (size_t i = 0; i < Count; ++i) {
        if (!command_fields_equal(result.canonical[i], result.decoded[i]))
            evidence.decoded_equal = false;
    }
    return evidence;
}

template <size_t BatchCount>
bool move_batch_prefix_equal(
    const round_trip_result_t<3> &move,
    const round_trip_result_t<BatchCount> &batch)
{
    static_assert(BatchCount >= 3);
    for (size_t i = 0; i < move.canonical.size(); ++i) {
        if (!command_fields_equal(move.canonical[i], batch.canonical[i]) ||
            !command_fields_equal(move.decoded[i], batch.decoded[i]) ||
            !command_fields_equal(move.canonical[i], move.decoded[i]) ||
            !command_fields_equal(batch.canonical[i], batch.decoded[i])) {
            return false;
        }
        const worr_prediction_command_v1 move_command =
            prediction_command(move.decoded[i]);
        const worr_prediction_command_v1 batch_command =
            prediction_command(batch.decoded[i]);
        if (Worr_PredictionHashCommandV1(&move_command) !=
            Worr_PredictionHashCommandV1(&batch_command)) {
            return false;
        }
    }
    return true;
}

const char *json_bool(bool value)
{
    return value ? "true" : "false";
}

void print_command_json(const usercmd_t &command)
{
    const worr_prediction_command_v1 prediction =
        prediction_command(command);
    std::printf(
        "{\"duration_ms\":%u,\"buttons\":%u,"
        "\"angle_float_bits\":[\"%08x\",\"%08x\",\"%08x\"],"
        "\"forward_move_float_bits\":\"%08x\","
        "\"side_move_float_bits\":\"%08x\","
        "\"prediction_hash\":\"%016llx\"}",
        static_cast<unsigned>(command.msec),
        static_cast<unsigned>(command.buttons),
        static_cast<unsigned>(std::bit_cast<uint32_t>(command.angles[0])),
        static_cast<unsigned>(std::bit_cast<uint32_t>(command.angles[1])),
        static_cast<unsigned>(std::bit_cast<uint32_t>(command.angles[2])),
        static_cast<unsigned>(std::bit_cast<uint32_t>(command.forwardmove)),
        static_cast<unsigned>(std::bit_cast<uint32_t>(command.sidemove)),
        static_cast<unsigned long long>(
            Worr_PredictionHashCommandV1(&prediction)));
}

void print_command_array_json(const std::vector<usercmd_t> &commands)
{
    std::putchar('[');
    for (size_t i = 0; i < commands.size(); ++i) {
        if (i)
            std::putchar(',');
        print_command_json(commands[i]);
    }
    std::putchar(']');
}

void print_evidence_json(const std::vector<wire_evidence_t> &rows,
                         bool q2pro_move_batch_equal,
                         bool q2repro_move_batch_equal)
{
    std::printf(
        "{\"schema\":\"worr.networking.usercmd-live-wire-parity.v1\","
        "\"serverdata_serialized_and_parsed\":true,"
        "\"signed_short_angle_exhaustive_identity\":true,"
        "\"multi_turn_angle_stable\":true,"
        "\"atomic_rejection\":true,\"rows\":[");
    for (size_t i = 0; i < rows.size(); ++i) {
        const wire_evidence_t &row = rows[i];
        if (i)
            std::putchar(',');
        std::printf(
            "{\"protocol\":\"%s\",\"message\":\"%s\","
            "\"has_upmove\":%s,\"wire_bytes\":%zu,"
            "\"wire_sha256\":\"%s\","
            "\"canonical_idempotent\":%s,\"decoded_equal\":%s,"
            "\"canonical_commands\":",
            row.protocol.c_str(), row.message.c_str(),
            json_bool(row.has_upmove), row.wire_bytes,
            row.wire_sha256.c_str(), json_bool(row.canonical_idempotent),
            json_bool(row.decoded_equal));
        print_command_array_json(row.canonical);
        std::printf(",\"decoded_commands\":");
        print_command_array_json(row.decoded);
        std::putchar('}');
    }
    std::printf(
        "],\"move_batch_equivalence\":{"
        "\"q2pro\":%s,\"q2repro\":%s}}\n",
        json_bool(q2pro_move_batch_equal),
        json_bool(q2repro_move_batch_equal));
}

void validate_angle_canonicalization()
{
    for (uint32_t bits = 0; bits <= UINT16_MAX; ++bits) {
        q2proto_var_angles_t source{};
        q2proto_var_angles_set_short_comp(
            &source, 0, static_cast<int16_t>(static_cast<uint16_t>(bits)));
        const float decoded =
            q2proto_var_angles_get_float_comp(&source, 0);
        float canonical = 0.0f;
        require(NetUsercmd_CanonicalizeAngle(decoded, &canonical),
                "short-angle canonicalization rejected decoded value");
        q2proto_var_angles_t encoded{};
        q2proto_var_angles_set_float_comp(&encoded, 0, canonical);
        const uint16_t reencoded = static_cast<uint16_t>(
            q2proto_var_angles_get_short_comp(&encoded, 0));
        require(reencoded == bits,
                "short-angle failed exhaustive re-encode identity");
    }

    for (const float multi_turn :
         {3590.568115234375f, 999990.0625f, -999990.0625f}) {
        float canonical = 0.0f;
        require(NetUsercmd_CanonicalizeAngle(multi_turn, &canonical),
                "multi-turn angle canonicalization failed");
        q2proto_var_angles_t first{};
        q2proto_var_angles_set_float_comp(&first, 0, canonical);
        const int16_t short_value =
            q2proto_var_angles_get_short_comp(&first, 0);
        q2proto_var_angles_t second{};
        q2proto_var_angles_set_short_comp(&second, 0, short_value);
        require(equal_float(canonical,
                            q2proto_var_angles_get_float_comp(&second, 0)),
                "multi-turn canonical angle did not stabilize on wire");
    }

    q2proto_var_angles_t raw{};
    q2proto_var_angles_set_float_comp(&raw, 0, 3590.568115234375f);
    float canonical = 0.0f;
    require(NetUsercmd_CanonicalizeAngle(3590.568115234375f, &canonical),
            "known multi-turn regression canonicalization failed");
    q2proto_var_angles_t fixed{};
    q2proto_var_angles_set_float_comp(&fixed, 0, canonical);
    require(static_cast<uint16_t>(
                q2proto_var_angles_get_short_comp(&raw, 0)) == 63819u &&
                static_cast<uint16_t>(
                    q2proto_var_angles_get_short_comp(&fixed, 0)) == 63818u,
            "known multi-turn wire-rounding regression was not exercised");
}

void validate_fail_closed()
{
    usercmd_t invalid = make_command(
        16, std::numeric_limits<float>::infinity(), 0.0f, 0.0f, 0.0f,
        0.0f, 0);
    const usercmd_t invalid_before = invalid;
    require(!NetUsercmd_CanonicalizeForTransport(&invalid, false) &&
                std::memcmp(&invalid, &invalid_before, sizeof(invalid)) == 0,
            "non-finite command rejection was not atomic");

    invalid = make_command(16, 0.0f, 0.0f, 0.0f, 512.0f, 0.0f, 0);
    q2proto_clc_move_delta_t sentinel{};
    std::memset(&sentinel, 0xa5, sizeof(sentinel));
    const q2proto_clc_move_delta_t sentinel_before = sentinel;
    require(!NetUsercmd_BuildDelta(&sentinel, nullptr, &invalid, 0, false) &&
                std::memcmp(&sentinel, &sentinel_before,
                            sizeof(sentinel)) == 0,
            "out-of-range delta build rejection mutated output");

    q2proto_clc_move_delta_t invalid_delta{};
    invalid_delta.delta_bits = Q2P_CMD_MOVE_FORWARD;
    q2proto_var_coords_set_float_comp(&invalid_delta.move, 0, -513.0f);
    invalid_delta.msec = 16;
    usercmd_t output{};
    std::memset(&output, 0x5a, sizeof(output));
    const usercmd_t output_before = output;
    require(!NetUsercmd_ApplyDelta(&invalid_delta, nullptr, &output, false) &&
                std::memcmp(&output, &output_before, sizeof(output)) == 0,
            "out-of-range decoded delta rejection mutated output");

    usercmd_t legacy = make_command(
        16, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        BUTTON_ATTACK | BUTTON_HOLSTER | BUTTON_JUMP | BUTTON_CROUCH);
    require(NetUsercmd_CanonicalizeForTransport(&legacy, true) &&
                legacy.buttons == BUTTON_ATTACK,
            "legacy simultaneous vertical/HOLSTER canonicalization drifted");
    usercmd_t modern = make_command(
        16, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        BUTTON_ATTACK | BUTTON_HOLSTER | BUTTON_JUMP | BUTTON_CROUCH);
    require(NetUsercmd_CanonicalizeForTransport(&modern, false) &&
                modern.buttons ==
                    (BUTTON_ATTACK | BUTTON_HOLSTER | BUTTON_JUMP |
                     BUTTON_CROUCH),
            "modern button canonicalization lost representable bits");
}

void validate_prediction_command_conversion()
{
    usercmd_t command = make_command(
        19, 3590.568115234375f, -999990.0625f, 22.75f, 123.75f,
        -400.5f, BUTTON_ATTACK | BUTTON_JUMP);
    command.server_frame = UINT32_C(0x89abcdef);
    command.server_frame_delta = UINT32_C(0x01234567);
    std::array<uint8_t, sizeof(command)> command_before{};
    std::memcpy(command_before.data(), &command, sizeof(command));

    usercmd_t canonical = command;
    require(NetUsercmd_Canonicalize(&canonical),
            "prediction conversion fixture was not canonicalizable");

    worr_prediction_command_v1 expected{};
    expected.struct_size = sizeof(expected);
    expected.schema_version = WORR_PREDICTION_ABI_VERSION;
    expected.duration_ms = canonical.msec;
    expected.buttons = canonical.buttons;
    for (unsigned i = 0; i < 3; ++i)
        expected.view_angles[i] = canonical.angles[i];
    expected.forward_move = canonical.forwardmove;
    expected.side_move = canonical.sidemove;

    worr_prediction_command_v1 converted;
    std::memset(&converted, 0xa5, sizeof(converted));
    require(NetUsercmd_ToPredictionCommandV1(&command, &converted),
            "prediction command conversion rejected valid input");
    require(std::memcmp(&converted, &expected, sizeof(converted)) == 0,
            "prediction command conversion did not fully initialize the ABI record");
    require(std::memcmp(&command, command_before.data(), sizeof(command)) == 0,
            "prediction command conversion mutated its source");

    worr_prediction_command_v1 recanonicalized = converted;
    require(Worr_PredictionCanonicalizeCommandV1(&recanonicalized) &&
                std::memcmp(&recanonicalized, &converted,
                            sizeof(converted)) == 0,
            "converted prediction command was not canonically stable");

    union prediction_alias_storage_t {
        usercmd_t command;
        worr_prediction_command_v1 prediction;
    } aliased{};
    std::memcpy(&aliased.command, &command, sizeof(command));
    require(NetUsercmd_ToPredictionCommandV1(&aliased.command,
                                               &aliased.prediction),
            "same-address prediction command conversion failed");
    worr_prediction_command_v1 alias_result{};
    std::memcpy(&alias_result, &aliased, sizeof(alias_result));
    require(std::memcmp(&alias_result, &expected, sizeof(alias_result)) == 0,
            "same-address prediction command conversion corrupted its input");

    std::memset(&converted, 0x5a, sizeof(converted));
    const worr_prediction_command_v1 sentinel = converted;
    require(!NetUsercmd_ToPredictionCommandV1(nullptr, &converted) &&
                std::memcmp(&converted, &sentinel, sizeof(converted)) == 0,
            "null prediction command input mutated output");
    require(!NetUsercmd_ToPredictionCommandV1(&command, nullptr),
            "null prediction command output was accepted");

    usercmd_t invalid = command;
    invalid.angles[1] = std::numeric_limits<float>::infinity();
    require(!NetUsercmd_ToPredictionCommandV1(&invalid, &converted) &&
                std::memcmp(&converted, &sentinel, sizeof(converted)) == 0,
            "invalid prediction command input mutated output");

    prediction_alias_storage_t invalid_alias{};
    std::memcpy(&invalid_alias.command, &invalid, sizeof(invalid));
    prediction_alias_storage_t invalid_alias_before{};
    std::memcpy(&invalid_alias_before, &invalid_alias,
                sizeof(invalid_alias));
    require(!NetUsercmd_ToPredictionCommandV1(&invalid_alias.command,
                                                &invalid_alias.prediction) &&
                std::memcmp(&invalid_alias, &invalid_alias_before,
                            sizeof(invalid_alias)) == 0,
            "invalid aliased prediction command conversion was not atomic");
}

} // namespace

extern "C" uint8_t q2protoio_read_u8(uintptr_t io_arg)
{
    return read_little<uint8_t>(io_arg);
}

extern "C" uint16_t q2protoio_read_u16(uintptr_t io_arg)
{
    return read_little<uint16_t>(io_arg);
}

extern "C" uint32_t q2protoio_read_u32(uintptr_t io_arg)
{
    return read_little<uint32_t>(io_arg);
}

extern "C" uint64_t q2protoio_read_u64(uintptr_t io_arg)
{
    return read_little<uint64_t>(io_arg);
}

extern "C" q2proto_string_t q2protoio_read_string(uintptr_t io_arg)
{
    memory_io_t &io = io_from(io_arg);
    q2proto_string_t result{};
    result.str = reinterpret_cast<const char *>(io.data.data() + io.read_pos);
    while (io.read_pos < io.write_pos) {
        if (io.data[io.read_pos++] == 0)
            return result;
        ++result.len;
    }
    io.failed = true;
    result.str = nullptr;
    result.len = 0;
    return result;
}

extern "C" const void *q2protoio_read_raw(uintptr_t io_arg, size_t size,
                                            size_t *readcount)
{
    memory_io_t &io = io_from(io_arg);
    if (readcount) {
        const size_t available = io.write_pos - io.read_pos;
        const size_t amount = size < available ? size : available;
        const uint8_t *result = io.data.data() + io.read_pos;
        io.read_pos += amount;
        *readcount = amount;
        return result;
    }
    return read_exact(io, size);
}

extern "C" size_t q2protoio_read_available(uintptr_t io_arg)
{
    const memory_io_t &io = io_from(io_arg);
    return io.write_pos - io.read_pos;
}

extern "C" void q2protoio_write_u8(uintptr_t io_arg, uint8_t value)
{
    write_little(io_arg, value);
}

extern "C" void q2protoio_write_u16(uintptr_t io_arg, uint16_t value)
{
    write_little(io_arg, value);
}

extern "C" void q2protoio_write_u32(uintptr_t io_arg, uint32_t value)
{
    write_little(io_arg, value);
}

extern "C" void q2protoio_write_u64(uintptr_t io_arg, uint64_t value)
{
    write_little(io_arg, value);
}

extern "C" void *q2protoio_write_reserve_raw(uintptr_t io_arg, size_t size)
{
    return reserve_exact(io_from(io_arg), size);
}

extern "C" void q2protoio_write_raw(uintptr_t io_arg, const void *data,
                                      size_t size, size_t *written)
{
    memory_io_t &io = io_from(io_arg);
    const size_t available = io.data.size() - io.write_pos;
    const size_t amount = written && size > available ? available : size;
    if (!written && size > available) {
        io.failed = true;
        return;
    }
    void *destination = reserve_exact(io, amount);
    std::memcpy(destination, data, amount);
    if (written)
        *written = amount;
}

extern "C" size_t q2protoio_write_available(uintptr_t io_arg)
{
    const memory_io_t &io = io_from(io_arg);
    return io.data.size() - io.write_pos;
}

extern "C" q2proto_error_t q2protoerr_client_read(
    uintptr_t io_arg, q2proto_error_t error, const char *, ...)
{
    io_from(io_arg).codec_error = error;
    return error;
}

extern "C" q2proto_error_t q2protoerr_client_write(
    uintptr_t io_arg, q2proto_error_t error, const char *, ...)
{
    io_from(io_arg).codec_error = error;
    return error;
}

extern "C" q2proto_error_t q2protoerr_server_write(
    uintptr_t io_arg, q2proto_error_t error, const char *, ...)
{
    io_from(io_arg).codec_error = error;
    return error;
}

extern "C" q2proto_error_t q2protoerr_server_read(
    uintptr_t io_arg, q2proto_error_t error, const char *, ...)
{
    io_from(io_arg).codec_error = error;
    return error;
}

#if Q2PROTO_COMPRESSION_DEFLATE
/* This fixture exercises uncompressed command/serverdata paths.  Supplying
 * explicit fail-closed hooks keeps the same target linkable in zlib builds. */
extern "C" q2proto_error_t q2protoio_inflate_begin(
    uintptr_t io_arg, q2proto_inflate_deflate_header_mode_t,
    uintptr_t *inflate_io_arg)
{
    *inflate_io_arg = io_arg;
    return Q2P_ERR_NOT_IMPLEMENTED;
}

extern "C" q2proto_error_t q2protoio_inflate_data(uintptr_t, uintptr_t,
                                                     size_t)
{
    return Q2P_ERR_NOT_IMPLEMENTED;
}

extern "C" q2proto_error_t q2protoio_inflate_stream_ended(uintptr_t,
                                                            bool *stream_end)
{
    *stream_end = false;
    return Q2P_ERR_NOT_IMPLEMENTED;
}

extern "C" q2proto_error_t q2protoio_inflate_end(uintptr_t)
{
    return Q2P_ERR_NOT_IMPLEMENTED;
}

extern "C" q2proto_error_t q2protoio_deflate_begin(
    q2protoio_deflate_args_t *, size_t,
    q2proto_inflate_deflate_header_mode_t, uintptr_t *deflate_io_arg)
{
    *deflate_io_arg = 0;
    return Q2P_ERR_NOT_IMPLEMENTED;
}

extern "C" q2proto_error_t q2protoio_deflate_get_data(
    uintptr_t, size_t *in_size, const void **out, size_t *out_size)
{
    if (in_size)
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

int main(int argc, char **argv)
{
    const bool json = argc == 2 && std::string_view(argv[1]) == "--json";
    require(argc == 1 || json, "usage: usercmd_wire_parity_test [--json]");
    require(sha256_hex(nullptr, 0) ==
                "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "wire SHA-256 implementation self-test failed");
    static constexpr uint8_t sha256_abc[] = {'a', 'b', 'c'};
    require(sha256_hex(sha256_abc, sizeof(sha256_abc)) ==
                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "wire SHA-256 block self-test failed");
    validate_angle_canonicalization();
    validate_prediction_command_conversion();
    validate_fail_closed();

    std::vector<wire_evidence_t> evidence;

    protocol_fixture_t vanilla{};
    initialize_protocol(vanilla, "vanilla-default", Q2P_PROTOCOL_VANILLA,
                        Q2PROTO_GAME_VANILLA, false);
    const round_trip_result_t<3> vanilla_move = round_trip_default(vanilla);
    evidence.push_back(make_evidence(vanilla, vanilla_move));

    protocol_fixture_t q2pro_default{};
    initialize_protocol(q2pro_default, "q2pro-default",
                        Q2P_PROTOCOL_Q2PRO,
                        Q2PROTO_GAME_Q2PRO_EXTENDED_V2, false);
    const round_trip_result_t<3> q2pro_move =
        round_trip_default(q2pro_default);
    evidence.push_back(make_evidence(q2pro_default, q2pro_move));

    protocol_fixture_t q2pro_batch{};
    initialize_protocol(q2pro_batch, "q2pro-batch", Q2P_PROTOCOL_Q2PRO,
                        Q2PROTO_GAME_Q2PRO_EXTENDED_V2, true);
    const round_trip_result_t<8> q2pro_batch_result =
        round_trip_batch(q2pro_batch);
    evidence.push_back(make_evidence(q2pro_batch, q2pro_batch_result));

    protocol_fixture_t q2repro_default{};
    initialize_protocol(q2repro_default, "q2repro-default",
                        Q2P_PROTOCOL_Q2REPRO, Q2PROTO_GAME_RERELEASE,
                        false);
    const round_trip_result_t<3> q2repro_move =
        round_trip_default(q2repro_default);
    evidence.push_back(make_evidence(q2repro_default, q2repro_move));

    protocol_fixture_t q2repro_batch{};
    initialize_protocol(q2repro_batch, "q2repro-batch",
                        Q2P_PROTOCOL_Q2REPRO, Q2PROTO_GAME_RERELEASE,
                        true);
    const round_trip_result_t<8> q2repro_batch_result =
        round_trip_batch(q2repro_batch);
    evidence.push_back(make_evidence(q2repro_batch, q2repro_batch_result));

    const bool q2pro_move_batch_equal =
        move_batch_prefix_equal(q2pro_move, q2pro_batch_result);
    const bool q2repro_move_batch_equal =
        move_batch_prefix_equal(q2repro_move, q2repro_batch_result);
    require(q2pro_move_batch_equal,
            "Q2PRO MOVE and BATCH_MOVE canonical chains diverged");
    require(q2repro_move_batch_equal,
            "Q2REPRO MOVE and BATCH_MOVE canonical chains diverged");

    if (json) {
        print_evidence_json(evidence, q2pro_move_batch_equal,
                            q2repro_move_batch_equal);
    } else {
        std::puts("usercmd live-wire parity tests passed");
    }
    return EXIT_SUCCESS;
}
