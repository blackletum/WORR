#include "client/demo_clock.h"
#include "common/net/consumed_cursor_sideband.h"
#include "shared/shared.h"
#include "common/protocol.h"

#include <cstdint>
#include <cstdio>

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, \
                         __LINE__, #condition);                            \
            return 1;                                                      \
        }                                                                  \
    } while (0)

static bool feed_anchor(int32_t frame, uint64_t time_us)
{
    worr_demo_clock_anchor_v1 anchor{};
    worr_demo_clock_setting_pair_v1 pairs[
        WORR_DEMO_CLOCK_SIDEBAND_PAIR_COUNT]{};
    if (!Worr_DemoClockAnchorInitV1(&anchor, frame, time_us) ||
        !Worr_DemoClockAnchorEncodeV1(
            &anchor, pairs, WORR_DEMO_CLOCK_SIDEBAND_PAIR_COUNT)) {
        return false;
    }
    for (const auto &pair : pairs) {
        bool handled = false;
        if (!CL_DemoClockObserveSetting(pair.index, pair.value, &handled) ||
            !handled) {
            return false;
        }
    }
    return true;
}

static bool consume_exact(int32_t frame, uint64_t expected_time)
{
    bool present = false;
    uint64_t time_us = 0;
    return CL_DemoClockConsumeFrame(frame, &present, &time_us) &&
           present && time_us == expected_time;
}

int main()
{
    bool handled = false;
    bool present = true;
    uint64_t time_us = UINT64_MAX;

    /* Legacy/live packets remain optional and cannot smuggle the private
     * reserved range into the canonical clock. */
    CL_DemoClockReset();
    CHECK(CL_DemoClockPacketBegin());
    CHECK(CL_DemoClockObserveSetting(SVS_FPS, 20, &handled));
    CHECK(!handled);
    CHECK(CL_DemoClockConsumeFrame(1, &present, &time_us));
    CHECK(!present && time_us == 0);
    CHECK(CL_DemoClockPacketEnd());

    CL_DemoClockReset();
    CHECK(CL_DemoClockPacketBegin());
    CHECK(!CL_DemoClockObserveSetting(
        WORR_DEMO_CLOCK_SETTING_BEGIN,
        WORR_DEMO_CLOCK_SIDEBAND_VERSION, &handled));
    CL_DemoClockReset();

    /* An explicitly armed snapshot accepts prefix gamestate services, then
     * requires FPS -> exact anchor -> optional cursor tuple -> exact frame. */
    CHECK(CL_DemoClockArmSyntheticPacket());
    CHECK(CL_DemoClockPacketBegin());
    CHECK(CL_DemoClockObserveInterveningService());
    CHECK(CL_DemoClockObserveSetting(SVS_FPS, 30, &handled));
    CHECK(!handled);
    CHECK(feed_anchor(91, UINT64_C(4567000)));
    CHECK(CL_DemoClockObserveSetting(
        WORR_CONSUMED_CURSOR_SETTING_BEGIN,
        WORR_CONSUMED_CURSOR_SIDEBAND_VERSION, &handled));
    CHECK(!handled);
    CHECK(consume_exact(91, UINT64_C(4567000)));
    CHECK(CL_DemoClockObserveInterveningService());
    CHECK(CL_DemoClockPacketEnd());

    /* Missing/mismatched anchors and dangling packet state fail closed. */
    CL_DemoClockReset();
    CHECK(CL_DemoClockArmSyntheticPacket());
    CHECK(CL_DemoClockPacketBegin());
    CHECK(CL_DemoClockObserveSetting(SVS_FPS, 60, &handled));
    CHECK(!CL_DemoClockConsumeFrame(7, &present, &time_us));
    CL_DemoClockReset();

    CHECK(CL_DemoClockArmSyntheticPacket());
    CHECK(CL_DemoClockPacketBegin());
    CHECK(CL_DemoClockObserveSetting(SVS_FPS, 60, &handled));
    CHECK(feed_anchor(7, 7000));
    CHECK(!CL_DemoClockConsumeFrame(8, &present, &time_us));
    CL_DemoClockReset();

    CHECK(CL_DemoClockArmSyntheticPacket());
    CHECK(CL_DemoClockPacketBegin());
    CHECK(CL_DemoClockObserveSetting(SVS_FPS, 20, &handled));
    CHECK(CL_DemoClockObserveSetting(
        WORR_DEMO_CLOCK_SETTING_BEGIN,
        WORR_DEMO_CLOCK_SIDEBAND_VERSION, &handled));
    CHECK(!CL_DemoClockPacketEnd());

    /* A pristine synthetic prefix survives the logical packet restart caused
     * by svc_serverdata; partial or post-FPS state is never preserved. */
    CL_DemoClockReset();
    CHECK(CL_DemoClockArmSyntheticPacket());
    CHECK(CL_DemoClockPacketBegin());
    CHECK(CL_DemoClockObserveInterveningService());
    CL_DemoClockServerDataReset();
    CHECK(CL_DemoClockPacketBegin());
    CHECK(CL_DemoClockObserveSetting(SVS_FPS, 40, &handled));
    CHECK(feed_anchor(12, 900000));
    CHECK(consume_exact(12, 900000));
    CHECK(CL_DemoClockPacketEnd());

    CL_DemoClockShutdown();
    std::puts("demo clock runtime tests passed");
    return 0;
}
