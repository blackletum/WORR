#include <stddef.h>

#include "common/net/rewind.h"

_Static_assert(sizeof(worr_rewind_policy_state_v1) == 176,
               "C rewind policy state size changed");
_Static_assert(sizeof(worr_rewind_mapping_proof_v1) == 80,
               "C rewind mapping proof size changed");
_Static_assert(offsetof(worr_rewind_mapping_proof_v1, mapped_server_time_us) ==
                   64,
               "C rewind mapped-time offset changed");
_Static_assert(sizeof(worr_rewind_policy_decision_v1) == 80,
               "C rewind policy decision size changed");
_Static_assert(offsetof(worr_rewind_policy_decision_v1, requested_time_us) ==
                   48,
               "C rewind decision time offset changed");
_Static_assert(offsetof(worr_rewind_policy_state_v1, telemetry) == 72,
               "C rewind policy telemetry offset changed");
_Static_assert(sizeof(worr_rewind_pose_v1) == 160,
               "C rewind pose size changed");
_Static_assert(offsetof(worr_rewind_pose_v1, reserved0) == 60,
               "C rewind pose reserved offset changed");
_Static_assert(offsetof(worr_rewind_pose_v1, reserved1) == 156,
               "C rewind pose tail-reserved offset changed");
_Static_assert(sizeof(worr_rewind_history_telemetry_v1) == 144,
               "C rewind history telemetry size changed");
_Static_assert(sizeof(worr_rewind_scene_candidate_v1) == 184,
               "C rewind scene candidate size changed");
_Static_assert(offsetof(worr_rewind_scene_candidate_v1, pose) == 16,
               "C rewind scene pose offset changed");
_Static_assert(offsetof(worr_rewind_scene_candidate_v1, pose_hash) == 176,
               "C rewind scene pose hash offset changed");

int main(void) { return 0; }
