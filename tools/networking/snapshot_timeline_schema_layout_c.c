#include "common/net/snapshot_timeline.h"

#include <stddef.h>
#include <stdlib.h>

int main(void)
{
    return sizeof(worr_snapshot_timeline_slot_v1) == 592 &&
                   sizeof(worr_snapshot_timeline_entity_sample_v1) == 216 &&
                   offsetof(worr_snapshot_timeline_entity_sample_v1,
                            entity) == 72 &&
                   offsetof(worr_snapshot_timeline_clock_state_v1,
                            fractional_q16) == 44 &&
                   WORR_SNAPSHOT_TIMELINE_CORRUPT == 23
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
