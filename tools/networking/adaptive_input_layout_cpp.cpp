/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common/net/adaptive_input.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

template <typename T>
constexpr bool value_record =
    std::is_standard_layout_v<T> && std::is_trivially_copyable_v<T> &&
    std::is_trivially_destructible_v<T>;

static_assert(value_record<worr_adaptive_input_config_v1>);
static_assert(value_record<worr_adaptive_input_observation_v1>);
static_assert(value_record<worr_adaptive_input_output_v1>);
static_assert(value_record<worr_adaptive_input_state_v1>);
static_assert(value_record<worr_adaptive_input_telemetry_v1>);

static_assert(sizeof(worr_adaptive_input_config_v1) == 80);
static_assert(sizeof(worr_adaptive_input_observation_v1) == 72);
static_assert(sizeof(worr_adaptive_input_output_v1) == 88);
static_assert(sizeof(worr_adaptive_input_state_v1) == 168);
static_assert(sizeof(worr_adaptive_input_telemetry_v1) == 120);

static_assert(offsetof(worr_adaptive_input_observation_v1,
                       sample_time_ms) == 8);
static_assert(offsetof(worr_adaptive_input_output_v1,
                       decision_serial) == 16);
static_assert(offsetof(worr_adaptive_input_state_v1,
                       last_sample_time_ms) == 16);
static_assert(offsetof(worr_adaptive_input_telemetry_v1,
                       decision_serial) == 16);

static_assert(!std::is_pointer_v<
              decltype(worr_adaptive_input_observation_v1::sample_time_ms)>);
static_assert(!std::is_pointer_v<
              decltype(worr_adaptive_input_output_v1::reason_mask)>);
static_assert(!std::is_pointer_v<
              decltype(worr_adaptive_input_state_v1::decision_serial)>);
static_assert(!std::is_pointer_v<
              decltype(worr_adaptive_input_telemetry_v1::evaluate_calls)>);

int main()
{
    return WORR_ADAPTIVE_INPUT_VERSION == 1 ? 0 : 1;
}
