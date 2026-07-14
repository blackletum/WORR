/* Standalone FR-10-T09 authoritative consumed-cursor sideband tests. */

#include "common/net/consumed_cursor_sideband.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression)                                                   \
    do {                                                                    \
        if (!(expression)) {                                                \
            fprintf(stderr, "consumed_cursor_sideband_test:%d: %s\n",    \
                    __LINE__, #expression);                                 \
            exit(EXIT_FAILURE);                                             \
        }                                                                   \
    } while (0)

static uint32_t value_bits(int32_t value)
{
    uint32_t output;
    memcpy(&output, &value, sizeof(output));
    return output;
}

static int32_t value_from_bits(uint32_t value)
{
    int32_t output;
    memcpy(&output, &value, sizeof(output));
    return output;
}

static bool sideband_equal(const worr_consumed_cursor_sideband_v1 *a,
                           const worr_consumed_cursor_sideband_v1 *b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
}

static void make_pairs(
    worr_command_cursor_v1 cursor,
    worr_consumed_cursor_sideband_v1 *sideband,
    worr_consumed_cursor_setting_pair_v1 pairs[
        WORR_CONSUMED_CURSOR_SIDEBAND_PAIR_COUNT])
{
    CHECK(Worr_ConsumedCursorSidebandInitV1(sideband, cursor));
    CHECK(Worr_ConsumedCursorSidebandValidateV1(sideband));
    CHECK(Worr_ConsumedCursorSidebandEncodeV1(
        sideband, pairs, WORR_CONSUMED_CURSOR_SIDEBAND_PAIR_COUNT));
}

static void parser_begin(
    worr_consumed_cursor_sideband_parser_v1 *parser)
{
    CHECK(Worr_ConsumedCursorSidebandParserInitV1(parser));
    CHECK(Worr_ConsumedCursorSidebandPacketBeginV1(parser) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_PACKET_STARTED);
    CHECK(Worr_ConsumedCursorSidebandParserValidateV1(parser));
}

static void feed_header(
    worr_consumed_cursor_sideband_parser_v1 *parser,
    const worr_consumed_cursor_setting_pair_v1 pairs[
        WORR_CONSUMED_CURSOR_SIDEBAND_PAIR_COUNT])
{
    uint32_t i;
    for (i = 0; i < WORR_CONSUMED_CURSOR_SIDEBAND_PAIR_COUNT; ++i) {
        const worr_consumed_cursor_sideband_result_v1 result =
            Worr_ConsumedCursorSidebandObserveSettingV1(
                parser, pairs[i].index, pairs[i].value);
        CHECK(result ==
              (i + 1u == WORR_CONSUMED_CURSOR_SIDEBAND_PAIR_COUNT
                   ? WORR_CONSUMED_CURSOR_SIDEBAND_HEADER_COMMITTED
                   : WORR_CONSUMED_CURSOR_SIDEBAND_FIELD_ACCEPTED));
        CHECK(Worr_ConsumedCursorSidebandParserValidateV1(parser));
    }
}

static void test_record_and_signed_bit_round_trip(void)
{
    worr_consumed_cursor_sideband_v1 absent;
    worr_consumed_cursor_sideband_v1 high;
    worr_consumed_cursor_sideband_v1 decoded;
    worr_consumed_cursor_sideband_v1 before;
    worr_consumed_cursor_setting_pair_v1 pairs[
        WORR_CONSUMED_CURSOR_SIDEBAND_PAIR_COUNT];
    worr_consumed_cursor_setting_pair_v1 untouched[
        WORR_CONSUMED_CURSOR_SIDEBAND_PAIR_COUNT];
    worr_consumed_cursor_setting_pair_v1 untouched_before[
        WORR_CONSUMED_CURSOR_SIDEBAND_PAIR_COUNT];
    worr_consumed_cursor_sideband_parser_v1 parser;

    make_pairs((worr_command_cursor_v1){0, 0}, &absent, pairs);
    CHECK(value_bits(pairs[1].value) == 0);
    CHECK(value_bits(pairs[2].value) == 0);
    parser_begin(&parser);
    feed_header(&parser, pairs);
    memset(&decoded, 0xa5, sizeof(decoded));
    CHECK(Worr_ConsumedCursorSidebandConsumeFrameV1(&parser, &decoded) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_FRAME_MATCHED);
    CHECK(sideband_equal(&decoded, &absent));
    CHECK(Worr_ConsumedCursorSidebandPacketEndV1(&parser) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_PACKET_ENDED);

    make_pairs((worr_command_cursor_v1){UINT32_C(0x89abcdef),
                                        UINT32_C(0xfedcba98)},
               &high, pairs);
    CHECK(value_bits(pairs[1].value) == UINT32_C(0x89abcdef));
    CHECK(value_bits(pairs[2].value) == UINT32_C(0xfedcba98));
    CHECK(value_bits(pairs[3].value) == high.header_checksum);
    parser_begin(&parser);
    feed_header(&parser, pairs);
    CHECK(Worr_ConsumedCursorSidebandConsumeFrameV1(&parser, &decoded) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_FRAME_MATCHED);
    CHECK(sideband_equal(&decoded, &high));
    CHECK(Worr_ConsumedCursorSidebandPacketEndV1(&parser) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_PACKET_ENDED);

    CHECK(!Worr_ConsumedCursorSidebandInitV1(
        &decoded, (worr_command_cursor_v1){0, 1}));
    before = high;
    high.reserved0 = 1;
    CHECK(!Worr_ConsumedCursorSidebandValidateV1(&high));
    high = before;
    high.header_checksum ^= 1u;
    CHECK(!Worr_ConsumedCursorSidebandValidateV1(&high));
    high = before;

    memset(untouched, 0x3c, sizeof(untouched));
    memcpy(untouched_before, untouched, sizeof(untouched));
    CHECK(!Worr_ConsumedCursorSidebandEncodeV1(
        &high, untouched, WORR_CONSUMED_CURSOR_SIDEBAND_PAIR_COUNT - 1u));
    CHECK(memcmp(untouched, untouched_before, sizeof(untouched)) == 0);

    before = high;
    CHECK(!Worr_ConsumedCursorSidebandEncodeV1(
        &high, (worr_consumed_cursor_setting_pair_v1 *)(void *)&high,
        WORR_CONSUMED_CURSOR_SIDEBAND_PAIR_COUNT));
    CHECK(sideband_equal(&high, &before));
}

static void test_multiple_exact_frame_adjacency(void)
{
    worr_consumed_cursor_sideband_v1 first;
    worr_consumed_cursor_sideband_v1 second;
    worr_consumed_cursor_sideband_v1 output;
    worr_consumed_cursor_setting_pair_v1 first_pairs[
        WORR_CONSUMED_CURSOR_SIDEBAND_PAIR_COUNT];
    worr_consumed_cursor_setting_pair_v1 second_pairs[
        WORR_CONSUMED_CURSOR_SIDEBAND_PAIR_COUNT];
    worr_consumed_cursor_sideband_parser_v1 parser;

    make_pairs((worr_command_cursor_v1){12, 0}, &first, first_pairs);
    make_pairs((worr_command_cursor_v1){12, 44}, &second, second_pairs);
    parser_begin(&parser);
    CHECK(Worr_ConsumedCursorSidebandObserveSettingV1(
              &parser, 7, -9) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_NOT_SIDEBAND);
    feed_header(&parser, first_pairs);
    CHECK(Worr_ConsumedCursorSidebandConsumeFrameV1(&parser, &output) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_FRAME_MATCHED);
    CHECK(sideband_equal(&output, &first));
    feed_header(&parser, second_pairs);
    CHECK(Worr_ConsumedCursorSidebandConsumeFrameV1(&parser, &output) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_FRAME_MATCHED);
    CHECK(sideband_equal(&output, &second));
    CHECK(Worr_ConsumedCursorSidebandPacketEndV1(&parser) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_PACKET_ENDED);
    CHECK(parser.telemetry.frames_seen == 2);
    CHECK(parser.telemetry.frames_matched == 2);
    CHECK(parser.telemetry.headers_committed == 2);
    CHECK(parser.telemetry.non_sideband_settings == 1);
}

static void test_malformed_order_duplicates_and_version(void)
{
    worr_consumed_cursor_sideband_v1 sideband;
    worr_consumed_cursor_setting_pair_v1 pairs[
        WORR_CONSUMED_CURSOR_SIDEBAND_PAIR_COUNT];
    worr_consumed_cursor_sideband_parser_v1 parser;

    make_pairs((worr_command_cursor_v1){2, 9}, &sideband, pairs);

    parser_begin(&parser);
    CHECK(Worr_ConsumedCursorSidebandObserveSettingV1(
              &parser, pairs[1].index, pairs[1].value) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_UNEXPECTED_FIELD);
    CHECK(parser.phase == WORR_CONSUMED_CURSOR_PHASE_POISONED);
    CHECK(Worr_ConsumedCursorSidebandPacketEndV1(&parser) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_RESET_PACKET_BOUNDARY);

    parser_begin(&parser);
    CHECK(Worr_ConsumedCursorSidebandObserveSettingV1(
              &parser, pairs[0].index, pairs[0].value) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_FIELD_ACCEPTED);
    CHECK(Worr_ConsumedCursorSidebandObserveSettingV1(
              &parser, pairs[0].index, pairs[0].value) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_UNEXPECTED_FIELD);
    CHECK(Worr_ConsumedCursorSidebandPacketEndV1(&parser) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_RESET_PACKET_BOUNDARY);

    parser_begin(&parser);
    CHECK(Worr_ConsumedCursorSidebandObserveSettingV1(
              &parser, WORR_CONSUMED_CURSOR_SETTING_BEGIN,
              WORR_CONSUMED_CURSOR_SIDEBAND_VERSION + 1) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_UNSUPPORTED_VERSION);
    CHECK(parser.telemetry.unsupported_versions == 1);
    CHECK(Worr_ConsumedCursorSidebandPacketEndV1(&parser) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_RESET_PACKET_BOUNDARY);
}

static void test_checksum_commit_and_hostile_cursor(void)
{
    worr_consumed_cursor_sideband_v1 sideband;
    worr_consumed_cursor_sideband_v1 output;
    worr_consumed_cursor_setting_pair_v1 pairs[
        WORR_CONSUMED_CURSOR_SIDEBAND_PAIR_COUNT];
    worr_consumed_cursor_sideband_parser_v1 parser;
    uint32_t i;

    make_pairs((worr_command_cursor_v1){UINT32_MAX, UINT32_MAX},
               &sideband, pairs);
    parser_begin(&parser);
    for (i = 0; i < 3; ++i) {
        CHECK(Worr_ConsumedCursorSidebandObserveSettingV1(
                  &parser, pairs[i].index, pairs[i].value) ==
              WORR_CONSUMED_CURSOR_SIDEBAND_FIELD_ACCEPTED);
    }
    CHECK(Worr_ConsumedCursorSidebandObserveSettingV1(
              &parser, pairs[3].index, pairs[3].value ^ 1) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_CHECKSUM_MISMATCH);
    CHECK(parser.telemetry.checksum_failures == 1);
    CHECK(Worr_ConsumedCursorSidebandPacketEndV1(&parser) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_RESET_PACKET_BOUNDARY);

    parser_begin(&parser);
    for (i = 0; i < 4; ++i) {
        CHECK(Worr_ConsumedCursorSidebandObserveSettingV1(
                  &parser, pairs[i].index, pairs[i].value) ==
              WORR_CONSUMED_CURSOR_SIDEBAND_FIELD_ACCEPTED);
    }
    CHECK(Worr_ConsumedCursorSidebandObserveSettingV1(
              &parser, pairs[4].index, pairs[4].value ^ 1) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_COMMIT_MISMATCH);
    CHECK(parser.telemetry.commit_failures == 1);
    CHECK(Worr_ConsumedCursorSidebandPacketEndV1(&parser) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_RESET_PACKET_BOUNDARY);

    parser_begin(&parser);
    CHECK(Worr_ConsumedCursorSidebandObserveSettingV1(
              &parser, pairs[0].index, pairs[0].value) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_FIELD_ACCEPTED);
    CHECK(Worr_ConsumedCursorSidebandObserveSettingV1(
              &parser, pairs[1].index, 0) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_FIELD_ACCEPTED);
    CHECK(Worr_ConsumedCursorSidebandObserveSettingV1(
              &parser, pairs[2].index, 1) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_UNEXPECTED_FIELD);
    CHECK(Worr_ConsumedCursorSidebandPacketEndV1(&parser) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_RESET_PACKET_BOUNDARY);

    parser_begin(&parser);
    feed_header(&parser, pairs);
    CHECK(Worr_ConsumedCursorSidebandConsumeFrameV1(&parser, &output) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_FRAME_MATCHED);
    CHECK(output.consumed_cursor.epoch == UINT32_MAX);
    CHECK(output.consumed_cursor.contiguous_sequence == UINT32_MAX);
    CHECK(Worr_ConsumedCursorSidebandPacketEndV1(&parser) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_PACKET_ENDED);
}

static void test_intervening_services_and_missing_frame_header(void)
{
    worr_consumed_cursor_sideband_v1 sideband;
    worr_consumed_cursor_sideband_v1 output;
    worr_consumed_cursor_sideband_v1 untouched;
    worr_consumed_cursor_setting_pair_v1 pairs[
        WORR_CONSUMED_CURSOR_SIDEBAND_PAIR_COUNT];
    worr_consumed_cursor_sideband_parser_v1 parser;

    make_pairs((worr_command_cursor_v1){7, 31}, &sideband, pairs);
    memset(&untouched, 0x6d, sizeof(untouched));

    parser_begin(&parser);
    output = untouched;
    CHECK(Worr_ConsumedCursorSidebandConsumeFrameV1(&parser, &output) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_MISSING_HEADER);
    CHECK(memcmp(&output, &untouched, sizeof(output)) == 0);
    CHECK(parser.phase == WORR_CONSUMED_CURSOR_PHASE_POISONED);
    CHECK(Worr_ConsumedCursorSidebandPacketEndV1(&parser) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_RESET_PACKET_BOUNDARY);

    parser_begin(&parser);
    CHECK(Worr_ConsumedCursorSidebandObserveSettingV1(
              &parser, pairs[0].index, pairs[0].value) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_FIELD_ACCEPTED);
    CHECK(Worr_ConsumedCursorSidebandObserveSettingV1(
              &parser, 123, 456) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_RESET_INTERVENING_SERVICE);
    CHECK(parser.phase == WORR_CONSUMED_CURSOR_PHASE_POISONED);
    CHECK(Worr_ConsumedCursorSidebandPacketEndV1(&parser) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_RESET_PACKET_BOUNDARY);

    parser_begin(&parser);
    feed_header(&parser, pairs);
    CHECK(Worr_ConsumedCursorSidebandObserveInterveningServiceV1(&parser) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_RESET_INTERVENING_SERVICE);
    CHECK(Worr_ConsumedCursorSidebandPacketEndV1(&parser) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_RESET_PACKET_BOUNDARY);

    parser_begin(&parser);
    CHECK(Worr_ConsumedCursorSidebandObserveSettingV1(
              &parser, pairs[0].index, pairs[0].value) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_FIELD_ACCEPTED);
    CHECK(Worr_ConsumedCursorSidebandConsumeFrameV1(&parser, &output) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_MISSING_HEADER);
    CHECK(parser.phase == WORR_CONSUMED_CURSOR_PHASE_POISONED);
    CHECK(Worr_ConsumedCursorSidebandPacketEndV1(&parser) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_RESET_PACKET_BOUNDARY);

    parser_begin(&parser);
    feed_header(&parser, pairs);
    CHECK(Worr_ConsumedCursorSidebandObserveSettingV1(
              &parser, 9, 9) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_RESET_INTERVENING_SERVICE);
    CHECK(Worr_ConsumedCursorSidebandPacketEndV1(&parser) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_RESET_PACKET_BOUNDARY);
}

static void test_exhaustive_single_bit_corruption(void)
{
    worr_consumed_cursor_sideband_v1 sideband;
    worr_consumed_cursor_setting_pair_v1 clean[
        WORR_CONSUMED_CURSOR_SIDEBAND_PAIR_COUNT];
    uint32_t field;
    uint32_t bit;

    make_pairs((worr_command_cursor_v1){UINT32_C(0x89abcdef),
                                        UINT32_C(0xfedcba98)},
               &sideband, clean);
    for (field = 0;
         field < WORR_CONSUMED_CURSOR_SIDEBAND_PAIR_COUNT; ++field) {
        for (bit = 0; bit < 32; ++bit) {
            worr_consumed_cursor_setting_pair_v1 hostile[
                WORR_CONSUMED_CURSOR_SIDEBAND_PAIR_COUNT];
            worr_consumed_cursor_sideband_parser_v1 parser;
            uint32_t i;
            bool rejected = false;
            memcpy(hostile, clean, sizeof(hostile));
            hostile[field].value = value_from_bits(
                value_bits(hostile[field].value) ^
                (UINT32_C(1) << bit));
            parser_begin(&parser);
            for (i = 0;
                 i < WORR_CONSUMED_CURSOR_SIDEBAND_PAIR_COUNT; ++i) {
                const worr_consumed_cursor_sideband_result_v1 result =
                    Worr_ConsumedCursorSidebandObserveSettingV1(
                        &parser, hostile[i].index, hostile[i].value);
                CHECK(Worr_ConsumedCursorSidebandParserValidateV1(&parser));
                if (result != WORR_CONSUMED_CURSOR_SIDEBAND_FIELD_ACCEPTED &&
                    result !=
                        WORR_CONSUMED_CURSOR_SIDEBAND_HEADER_COMMITTED) {
                    rejected = true;
                    break;
                }
            }
            CHECK(rejected);
            CHECK(parser.phase == WORR_CONSUMED_CURSOR_PHASE_POISONED);
            CHECK(Worr_ConsumedCursorSidebandPacketEndV1(&parser) ==
                  WORR_CONSUMED_CURSOR_SIDEBAND_RESET_PACKET_BOUNDARY);
        }
    }
}

static void test_packet_boundaries_aliases_and_transactionality(void)
{
    worr_consumed_cursor_sideband_v1 sideband;
    worr_consumed_cursor_sideband_v1 output;
    worr_consumed_cursor_setting_pair_v1 pairs[
        WORR_CONSUMED_CURSOR_SIDEBAND_PAIR_COUNT];
    worr_consumed_cursor_sideband_parser_v1 parser;

    make_pairs((worr_command_cursor_v1){9, 10}, &sideband, pairs);
    parser_begin(&parser);
    CHECK(Worr_ConsumedCursorSidebandPacketBeginV1(&parser) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_RESET_PACKET_BOUNDARY);
    CHECK(parser.telemetry.packet_boundary_failures == 1);
    CHECK(Worr_ConsumedCursorSidebandPacketEndV1(&parser) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_PACKET_ENDED);

    parser_begin(&parser);
    CHECK(Worr_ConsumedCursorSidebandObserveSettingV1(
              &parser, pairs[0].index, pairs[0].value) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_FIELD_ACCEPTED);
    CHECK(Worr_ConsumedCursorSidebandPacketEndV1(&parser) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_RESET_PACKET_BOUNDARY);
    CHECK(Worr_ConsumedCursorSidebandPacketBeginV1(&parser) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_PACKET_STARTED);
    CHECK(Worr_ConsumedCursorSidebandObserveSettingV1(
              &parser, pairs[1].index, pairs[1].value) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_UNEXPECTED_FIELD);
    CHECK(Worr_ConsumedCursorSidebandPacketEndV1(&parser) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_RESET_PACKET_BOUNDARY);

    parser_begin(&parser);
    feed_header(&parser, pairs);
    CHECK(Worr_ConsumedCursorSidebandConsumeFrameV1(
              &parser, &parser.pending) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_INVALID_ARGUMENT);
    CHECK(parser.phase == WORR_CONSUMED_CURSOR_PHASE_FRAME);
    CHECK(parser.telemetry.invalid_arguments == 1);
    CHECK(Worr_ConsumedCursorSidebandConsumeFrameV1(&parser, &output) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_FRAME_MATCHED);
    CHECK(sideband_equal(&output, &sideband));
    CHECK(Worr_ConsumedCursorSidebandPacketEndV1(&parser) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_PACKET_ENDED);

    CHECK(Worr_ConsumedCursorSidebandPacketEndV1(&parser) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_INVALID_STATE);
    CHECK(parser.telemetry.invalid_state == 1);
}

static void test_saturating_telemetry_and_invalid_state(void)
{
    worr_consumed_cursor_sideband_v1 sideband;
    worr_consumed_cursor_sideband_v1 output;
    worr_consumed_cursor_setting_pair_v1 pairs[
        WORR_CONSUMED_CURSOR_SIDEBAND_PAIR_COUNT];
    worr_consumed_cursor_sideband_parser_v1 parser;

    make_pairs((worr_command_cursor_v1){3, 4}, &sideband, pairs);
    CHECK(Worr_ConsumedCursorSidebandParserInitV1(&parser));
    memset(&parser.telemetry, 0xff, sizeof(parser.telemetry));
    CHECK(Worr_ConsumedCursorSidebandPacketBeginV1(&parser) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_PACKET_STARTED);
    feed_header(&parser, pairs);
    CHECK(Worr_ConsumedCursorSidebandConsumeFrameV1(&parser, &output) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_FRAME_MATCHED);
    CHECK(Worr_ConsumedCursorSidebandPacketEndV1(&parser) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_PACKET_ENDED);
    {
        const unsigned char *bytes =
            (const unsigned char *)(const void *)&parser.telemetry;
        size_t i;
        for (i = 0; i < sizeof(parser.telemetry); ++i)
            CHECK(bytes[i] == UCHAR_MAX);
    }

    CHECK(Worr_ConsumedCursorSidebandParserInitV1(&parser));
    parser.phase = UINT32_MAX;
    CHECK(!Worr_ConsumedCursorSidebandParserValidateV1(&parser));
    CHECK(Worr_ConsumedCursorSidebandPacketBeginV1(&parser) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_INVALID_STATE);
    CHECK(Worr_ConsumedCursorSidebandPacketBeginV1(NULL) ==
          WORR_CONSUMED_CURSOR_SIDEBAND_INVALID_ARGUMENT);
}

int main(void)
{
    test_record_and_signed_bit_round_trip();
    test_multiple_exact_frame_adjacency();
    test_malformed_order_duplicates_and_version();
    test_checksum_commit_and_hostile_cursor();
    test_intervening_services_and_missing_frame_header();
    test_exhaustive_single_bit_corruption();
    test_packet_boundaries_aliases_and_transactionality();
    test_saturating_telemetry_and_invalid_state();
    puts("consumed_cursor_sideband_test: ok");
    return EXIT_SUCCESS;
}
