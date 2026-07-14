#include "common/net/rewind_observation.h"

#include <type_traits>

static_assert(std::is_standard_layout_v<worr_rewind_observation_v1>);
static_assert(std::is_trivially_copyable_v<worr_rewind_observation_v1>);
static_assert(sizeof(worr_rewind_observation_v1) == 160);

int main() { return 0; }
