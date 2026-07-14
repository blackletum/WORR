#include "common/net/snapshot_recovery.h"

#include <stddef.h>

_Static_assert(WORR_SNAPSHOT_RECOVERY_VERSION == 1,
               "snapshot recovery version changed");
_Static_assert(offsetof(worr_snapshot_recovery_config_v1,
                        legacy_failure_threshold) == 8,
               "snapshot recovery config header changed");
_Static_assert(offsetof(worr_snapshot_recovery_observation_v1,
                        snapshot_number) == 16,
               "snapshot recovery observation header changed");
_Static_assert(offsetof(worr_snapshot_recovery_decision_v1,
                        request_generation) == 16,
               "snapshot recovery decision header changed");
_Static_assert(offsetof(worr_snapshot_recovery_state_v1, observations) == 48,
               "snapshot recovery state counters moved");
_Static_assert(offsetof(worr_snapshot_recovery_state_v1, recoveries) == 112,
               "snapshot recovery state tail moved");

int main(void)
{
    return 0;
}
