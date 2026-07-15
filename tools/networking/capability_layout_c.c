#include "common/net/capability.h"

_Static_assert(WORR_NET_CAP_NATIVE_EVENT_STREAM_V1 == (UINT32_C(1) << 5),
               "native event stream capability bit");
_Static_assert(WORR_NET_CAP_NATIVE_EPOCH_CANCEL_V1 == (UINT32_C(1) << 6),
               "native epoch cancellation capability bit");
_Static_assert(WORR_NET_CAP_KNOWN_MASK == UINT32_C(127),
               "known capability mask");
_Static_assert(WORR_NET_CAP_LEGACY_STAGE_MASK == UINT32_C(0x03),
               "public legacy-stage offer mask");
_Static_assert(WORR_NET_CAP_NATIVE_READINESS_REQUIRED_MASK ==
                   UINT32_C(0x50),
               "native readiness required mask");
_Static_assert(WORR_NET_CAP_NATIVE_COMMAND_PRIVATE_MASK == UINT32_C(0x53),
               "native command private mask");
_Static_assert(WORR_NET_CAP_NATIVE_EVENT_PRIVATE_MASK == UINT32_C(0x73),
               "native event private mask");
_Static_assert((WORR_NET_CAP_LEGACY_STAGE_MASK &
                (WORR_NET_CAP_NATIVE_EVENT_STREAM_V1 |
                 WORR_NET_CAP_NATIVE_EPOCH_CANCEL_V1)) == 0,
               "private native bits remain outside the legacy stage");

int main(void)
{
    return sizeof(worr_net_capability_state_v1) == 32 ? 0 : 1;
}
