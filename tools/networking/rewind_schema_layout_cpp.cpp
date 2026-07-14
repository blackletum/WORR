#include <cstddef>
#include <type_traits>

#include "common/net/rewind.h"

static_assert(std::is_standard_layout_v<worr_rewind_policy_state_v1>);
static_assert(std::is_standard_layout_v<worr_rewind_pose_v1>);
static_assert(std::is_standard_layout_v<worr_rewind_scene_candidate_v1>);
static_assert(sizeof(worr_rewind_policy_state_v1) == 176);
static_assert(sizeof(worr_rewind_mapping_proof_v1) == 80);
static_assert(offsetof(worr_rewind_mapping_proof_v1, mapped_server_time_us) ==
              64);
static_assert(sizeof(worr_rewind_policy_decision_v1) == 80);
static_assert(offsetof(worr_rewind_policy_decision_v1, requested_time_us) ==
              48);
static_assert(offsetof(worr_rewind_policy_state_v1, telemetry) == 72);
static_assert(sizeof(worr_rewind_pose_v1) == 160);
static_assert(offsetof(worr_rewind_pose_v1, reserved0) == 60);
static_assert(offsetof(worr_rewind_pose_v1, reserved1) == 156);
static_assert(sizeof(worr_rewind_history_telemetry_v1) == 144);
static_assert(sizeof(worr_rewind_scene_candidate_v1) == 184);
static_assert(offsetof(worr_rewind_scene_candidate_v1, pose) == 16);
static_assert(offsetof(worr_rewind_scene_candidate_v1, pose_hash) == 176);

int main() { return 0; }
