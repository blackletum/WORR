/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/demo_clock_sideband.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__,      \
                    __LINE__, #condition);                                 \
            return 1;                                                      \
        }                                                                  \
    } while (0)

static int parse_pairs(
    const worr_demo_clock_setting_pair_v1 *pairs,
    int32_t expected_frame,
    uint64_t expected_time)
{
    worr_demo_clock_sideband_parser_v1 parser;
    worr_demo_clock_anchor_v1 output;
    uint32_t i;
    CHECK(Worr_DemoClockSidebandParserInitV1(&parser));
    CHECK(Worr_DemoClockSidebandPacketBeginV1(&parser) ==
          WORR_DEMO_CLOCK_SIDEBAND_PACKET_STARTED);
    for (i = 0; i < WORR_DEMO_CLOCK_SIDEBAND_PAIR_COUNT; ++i) {
        const worr_demo_clock_sideband_result_v1 result =
            Worr_DemoClockSidebandObserveSettingV1(
                &parser, pairs[i].index, pairs[i].value);
        CHECK(result ==
                  WORR_DEMO_CLOCK_SIDEBAND_FIELD_ACCEPTED ||
              result ==
                  WORR_DEMO_CLOCK_SIDEBAND_ANCHOR_COMMITTED);
    }
    memset(&output, 0, sizeof(output));
    CHECK(Worr_DemoClockSidebandConsumeFrameV1(
              &parser, expected_frame, &output) ==
          WORR_DEMO_CLOCK_SIDEBAND_FRAME_MATCHED);
    CHECK(output.server_frame == expected_frame);
    CHECK(output.server_time_us == expected_time);
    CHECK(Worr_DemoClockSidebandPacketEndV1(&parser) ==
          WORR_DEMO_CLOCK_SIDEBAND_PACKET_ENDED);
    return 0;
}

int main(void)
{
    const int32_t frame = 12345;
    const uint64_t time_us = UINT64_C(0xfedcba9876543210);
    worr_demo_clock_anchor_v1 anchor;
    worr_demo_clock_setting_pair_v1 pairs[
        WORR_DEMO_CLOCK_SIDEBAND_PAIR_COUNT];
    worr_demo_clock_sideband_parser_v1 parser;
    worr_demo_clock_anchor_v1 output;
    uint32_t i;

    CHECK(Worr_DemoClockSettingRecognizedV1(
        WORR_DEMO_CLOCK_SETTING_BEGIN));
    CHECK(Worr_DemoClockSettingRecognizedV1(
        WORR_DEMO_CLOCK_SETTING_COMMIT));
    CHECK(!Worr_DemoClockSettingRecognizedV1(-31781));
    CHECK(!Worr_DemoClockSettingRecognizedV1(-31774));

    CHECK(Worr_DemoClockAnchorInitV1(&anchor, frame, time_us));
    CHECK(Worr_DemoClockAnchorValidateV1(&anchor));
    CHECK(!Worr_DemoClockAnchorInitV1(&anchor, -1, time_us));
    CHECK(Worr_DemoClockAnchorInitV1(&anchor, frame, time_us));
    CHECK(Worr_DemoClockAnchorEncodeV1(
        &anchor, pairs, WORR_DEMO_CLOCK_SIDEBAND_PAIR_COUNT));
    CHECK(parse_pairs(pairs, frame, time_us) == 0);

    anchor.reserved0 = 1;
    CHECK(!Worr_DemoClockAnchorValidateV1(&anchor));
    anchor.reserved0 = 0;
    CHECK(!Worr_DemoClockAnchorEncodeV1(
        &anchor, (worr_demo_clock_setting_pair_v1 *)&anchor,
        WORR_DEMO_CLOCK_SIDEBAND_PAIR_COUNT));

    CHECK(Worr_DemoClockSidebandParserInitV1(&parser));
    CHECK(Worr_DemoClockSidebandPacketBeginV1(&parser) ==
          WORR_DEMO_CLOCK_SIDEBAND_PACKET_STARTED);
    CHECK(Worr_DemoClockSidebandPacketBeginV1(&parser) ==
          WORR_DEMO_CLOCK_SIDEBAND_PACKET_BOUNDARY);
    CHECK(Worr_DemoClockSidebandPacketEndV1(&parser) ==
          WORR_DEMO_CLOCK_SIDEBAND_PACKET_ENDED);

    CHECK(Worr_DemoClockSidebandParserInitV1(&parser));
    CHECK(Worr_DemoClockSidebandPacketBeginV1(&parser) ==
          WORR_DEMO_CLOCK_SIDEBAND_PACKET_STARTED);
    CHECK(Worr_DemoClockSidebandObserveSettingV1(
              &parser, pairs[0].index, pairs[0].value) ==
          WORR_DEMO_CLOCK_SIDEBAND_FIELD_ACCEPTED);
    CHECK(Worr_DemoClockSidebandObserveInterveningServiceV1(
              &parser) ==
          WORR_DEMO_CLOCK_SIDEBAND_INTERVENING_SERVICE);
    CHECK(Worr_DemoClockSidebandPacketEndV1(&parser) ==
          WORR_DEMO_CLOCK_SIDEBAND_PACKET_BOUNDARY);

    for (i = 0; i < WORR_DEMO_CLOCK_SIDEBAND_PAIR_COUNT; ++i) {
        worr_demo_clock_setting_pair_v1 corrupted[
            WORR_DEMO_CLOCK_SIDEBAND_PAIR_COUNT];
        uint32_t j;
        bool rejected = false;
        memcpy(corrupted, pairs, sizeof(corrupted));
        corrupted[i].value ^= 1;
        CHECK(Worr_DemoClockSidebandParserInitV1(&parser));
        CHECK(Worr_DemoClockSidebandPacketBeginV1(&parser) ==
              WORR_DEMO_CLOCK_SIDEBAND_PACKET_STARTED);
        for (j = 0; j < WORR_DEMO_CLOCK_SIDEBAND_PAIR_COUNT; ++j) {
            const worr_demo_clock_sideband_result_v1 result =
                Worr_DemoClockSidebandObserveSettingV1(
                    &parser, corrupted[j].index, corrupted[j].value);
            if (result != WORR_DEMO_CLOCK_SIDEBAND_FIELD_ACCEPTED &&
                result != WORR_DEMO_CLOCK_SIDEBAND_ANCHOR_COMMITTED) {
                rejected = true;
                break;
            }
        }
        if (!rejected) {
            memset(&output, 0, sizeof(output));
            rejected = Worr_DemoClockSidebandConsumeFrameV1(
                           &parser, frame, &output) !=
                       WORR_DEMO_CLOCK_SIDEBAND_FRAME_MATCHED;
        }
        CHECK(rejected);
    }

    CHECK(Worr_DemoClockSidebandParserInitV1(&parser));
    CHECK(Worr_DemoClockSidebandPacketBeginV1(&parser) ==
          WORR_DEMO_CLOCK_SIDEBAND_PACKET_STARTED);
    for (i = 0; i < WORR_DEMO_CLOCK_SIDEBAND_PAIR_COUNT; ++i) {
        CHECK(Worr_DemoClockSidebandObserveSettingV1(
                  &parser, pairs[i].index, pairs[i].value) ==
              (i + 1 == WORR_DEMO_CLOCK_SIDEBAND_PAIR_COUNT
                   ? WORR_DEMO_CLOCK_SIDEBAND_ANCHOR_COMMITTED
                   : WORR_DEMO_CLOCK_SIDEBAND_FIELD_ACCEPTED));
    }
    memset(&output, 0, sizeof(output));
    CHECK(Worr_DemoClockSidebandConsumeFrameV1(
              &parser, frame + 1, &output) ==
          WORR_DEMO_CLOCK_SIDEBAND_FRAME_MISMATCH);

    puts("demo clock sideband tests passed");
    return 0;
}
