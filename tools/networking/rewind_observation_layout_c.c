#include "common/net/rewind_observation.h"

int main(void) {
  return sizeof(worr_rewind_observation_v1) == 160 &&
                 sizeof(worr_rewind_observation_telemetry_v1) == 128
             ? 0
             : 1;
}
