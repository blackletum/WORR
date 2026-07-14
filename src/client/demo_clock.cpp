/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "client/demo_clock.h"
#include "common/net/consumed_cursor_sideband.h"
#include "shared/shared.h"
#include "common/protocol.h"

#include <cstring>

namespace {

worr_demo_clock_sideband_parser_v1 parser{};
bool initialized{};
bool synthetic_armed{};
bool synthetic_active{};
bool need_fps{};
bool saw_frame{};

bool ensure_initialized()
{
    if (initialized)
        return Worr_DemoClockSidebandParserValidateV1(&parser);
    initialized = Worr_DemoClockSidebandParserInitV1(&parser);
    return initialized;
}

bool consumed_cursor_setting(int32_t index)
{
    return index >= WORR_CONSUMED_CURSOR_SETTING_BEGIN &&
           index <= WORR_CONSUMED_CURSOR_SETTING_COMMIT;
}

} // namespace

extern "C" void CL_DemoClockReset(void)
{
    std::memset(&parser, 0, sizeof(parser));
    initialized = Worr_DemoClockSidebandParserInitV1(&parser);
    synthetic_armed = false;
    synthetic_active = false;
    need_fps = false;
    saw_frame = false;
}

extern "C" void CL_DemoClockServerDataReset(void)
{
    const bool rearm_synthetic =
        synthetic_active && parser.packet_active && need_fps &&
        !saw_frame && parser.phase == WORR_DEMO_CLOCK_PHASE_IDLE;
    CL_DemoClockReset();
    synthetic_armed = rearm_synthetic;
}

extern "C" void CL_DemoClockShutdown(void)
{
    std::memset(&parser, 0, sizeof(parser));
    initialized = false;
    synthetic_armed = false;
    synthetic_active = false;
    need_fps = false;
    saw_frame = false;
}

extern "C" bool CL_DemoClockArmSyntheticPacket(void)
{
    if (!ensure_initialized() || synthetic_armed || synthetic_active ||
        parser.packet_active) {
        return false;
    }
    synthetic_armed = true;
    return true;
}

extern "C" bool CL_DemoClockPacketBegin(void)
{
    if (!ensure_initialized())
        return false;
    const auto result = Worr_DemoClockSidebandPacketBeginV1(&parser);
    if (result != WORR_DEMO_CLOCK_SIDEBAND_PACKET_STARTED)
        return false;
    synthetic_active = synthetic_armed;
    synthetic_armed = false;
    need_fps = synthetic_active;
    saw_frame = false;
    return true;
}

extern "C" bool CL_DemoClockPacketEnd(void)
{
    if (!ensure_initialized())
        return false;
    const bool semantic_ok =
        !synthetic_active || (!need_fps && saw_frame);
    const auto result = Worr_DemoClockSidebandPacketEndV1(&parser);
    synthetic_active = false;
    need_fps = false;
    saw_frame = false;
    return semantic_ok &&
           result == WORR_DEMO_CLOCK_SIDEBAND_PACKET_ENDED;
}

extern "C" bool CL_DemoClockObserveSetting(int32_t index, int32_t value,
                                             bool *handled_out)
{
    if (!handled_out || !ensure_initialized() || !parser.packet_active)
        return false;
    *handled_out = false;

    if (Worr_DemoClockSettingRecognizedV1(index)) {
        *handled_out = true;
        if (!synthetic_active || need_fps)
            return false;
        const auto result = Worr_DemoClockSidebandObserveSettingV1(
            &parser, index, value);
        return result == WORR_DEMO_CLOCK_SIDEBAND_FIELD_ACCEPTED ||
               result == WORR_DEMO_CLOCK_SIDEBAND_ANCHOR_COMMITTED;
    }

    if (!synthetic_active)
        return parser.phase == WORR_DEMO_CLOCK_PHASE_IDLE;

    if (need_fps) {
        if (index == SVS_FPS)
            need_fps = false;
        /* Gamestate settings may precede the required FPS/anchor pair. */
        return true;
    }

    if (parser.phase == WORR_DEMO_CLOCK_PHASE_READY)
        return consumed_cursor_setting(index);
    if (parser.phase != WORR_DEMO_CLOCK_PHASE_IDLE)
        return false;

    /* The first anchor must immediately follow the snapshot's SVS_FPS.  Once
     * at least one exact frame has been consumed, ordinary tail services and
     * services between independently anchored frames are harmless. */
    return saw_frame;
}

extern "C" bool CL_DemoClockObserveInterveningService(void)
{
    if (!ensure_initialized() || !parser.packet_active)
        return false;
    if (!synthetic_active)
        return parser.phase == WORR_DEMO_CLOCK_PHASE_IDLE;
    if (need_fps)
        return true;
    if (parser.phase != WORR_DEMO_CLOCK_PHASE_IDLE) {
        (void)Worr_DemoClockSidebandObserveInterveningServiceV1(
            &parser);
        return false;
    }
    return saw_frame;
}

extern "C" bool CL_DemoClockConsumeFrame(
    int32_t server_frame, bool *present_out,
    uint64_t *server_time_us_out)
{
    if (!present_out || !server_time_us_out || server_frame < 0 ||
        !ensure_initialized() || !parser.packet_active) {
        return false;
    }
    *present_out = false;
    *server_time_us_out = 0;
    if (!synthetic_active)
        return parser.phase == WORR_DEMO_CLOCK_PHASE_IDLE;
    if (need_fps)
        return false;

    worr_demo_clock_anchor_v1 anchor{};
    if (Worr_DemoClockSidebandConsumeFrameV1(
            &parser, server_frame, &anchor) !=
        WORR_DEMO_CLOCK_SIDEBAND_FRAME_MATCHED) {
        return false;
    }
    *present_out = true;
    *server_time_us_out = anchor.server_time_us;
    saw_frame = true;
    return true;
}
