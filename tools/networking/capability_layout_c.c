#include "common/net/capability.h"

int main(void)
{
    return sizeof(worr_net_capability_state_v1) == 32 ? 0 : 1;
}
