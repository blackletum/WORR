#include "common/net/capability.h"

static_assert(sizeof(worr_net_capability_state_v1) == 32);
static_assert(WORR_NET_CAP_NATIVE_EVENT_STREAM_V1 == (UINT32_C(1) << 5));
static_assert(WORR_NET_CAP_NATIVE_EPOCH_CANCEL_V1 == (UINT32_C(1) << 6));
static_assert(WORR_NET_CAP_KNOWN_MASK == UINT32_C(127));
static_assert(WORR_NET_CAP_LEGACY_STAGE_MASK == UINT32_C(0x03));
static_assert(WORR_NET_CAP_NATIVE_READINESS_REQUIRED_MASK == UINT32_C(0x50));
static_assert(WORR_NET_CAP_NATIVE_COMMAND_PRIVATE_MASK == UINT32_C(0x53));
static_assert(WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK == UINT32_C(0x73));
static_assert((WORR_NET_CAP_LEGACY_STAGE_MASK &
               (WORR_NET_CAP_NATIVE_EVENT_STREAM_V1 |
                WORR_NET_CAP_NATIVE_EPOCH_CANCEL_V1)) == 0);

int main()
{
    return 0;
}
