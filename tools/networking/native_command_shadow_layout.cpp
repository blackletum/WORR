/* C++20 consumer/layout proof for the isolated native command-shadow core. */
#include "common/net/native_command_shadow.h"

#include <cstddef>
#include <type_traits>

static_assert(WORR_NATIVE_COMMAND_SHADOW_ENCODED_BYTES == 110);
static_assert(WORR_NATIVE_COMMAND_SHADOW_PAYLOAD_CAPACITY == 64);
static_assert(WORR_NATIVE_COMMAND_SHADOW_JOIN_CAPACITY == 64);

static_assert(std::is_standard_layout_v<
              worr_native_command_shadow_builder_telemetry_v1>);
static_assert(std::is_trivially_copyable_v<
              worr_native_command_shadow_builder_telemetry_v1>);
static_assert(sizeof(worr_native_command_shadow_builder_telemetry_v1) == 64);

static_assert(std::is_standard_layout_v<
              worr_native_command_shadow_builder_v1>);
static_assert(std::is_trivially_copyable_v<
              worr_native_command_shadow_builder_v1>);
static_assert(sizeof(worr_native_command_shadow_builder_v1) == 96);
static_assert(offsetof(worr_native_command_shadow_builder_v1,
                       sample_time_us) == 16);
static_assert(offsetof(worr_native_command_shadow_builder_v1,
                       telemetry) == 32);

static_assert(std::is_standard_layout_v<
              worr_native_command_shadow_payload_slot_v1>);
static_assert(std::is_trivially_copyable_v<
              worr_native_command_shadow_payload_slot_v1>);
static_assert(sizeof(worr_native_command_shadow_payload_slot_v1) == 136);
static_assert(offsetof(worr_native_command_shadow_payload_slot_v1,
                       command_id) == 8);
static_assert(offsetof(worr_native_command_shadow_payload_slot_v1,
                       encoded) == 24);

static_assert(std::is_standard_layout_v<
              worr_native_command_shadow_payload_registry_v1>);
static_assert(std::is_trivially_copyable_v<
              worr_native_command_shadow_payload_registry_v1>);
static_assert(sizeof(worr_native_command_shadow_payload_registry_v1) == 8824);
static_assert(offsetof(worr_native_command_shadow_payload_registry_v1,
                       telemetry) == 24);
static_assert(offsetof(worr_native_command_shadow_payload_registry_v1,
                       slots) == 120);

static_assert(std::is_standard_layout_v<
              worr_native_command_shadow_comparator_v1>);
static_assert(std::is_trivially_copyable_v<
              worr_native_command_shadow_comparator_v1>);
static_assert(sizeof(worr_native_command_shadow_comparator_v1) == 88);
static_assert(offsetof(worr_native_command_shadow_comparator_v1,
                       sample_offset_us) == 16);
static_assert(offsetof(worr_native_command_shadow_comparator_v1,
                       telemetry) == 24);

static_assert(std::is_standard_layout_v<
              worr_native_command_shadow_compare_report_v1>);
static_assert(std::is_trivially_copyable_v<
              worr_native_command_shadow_compare_report_v1>);
static_assert(sizeof(worr_native_command_shadow_compare_report_v1) == 80);
static_assert(offsetof(worr_native_command_shadow_compare_report_v1,
                       native_command_id) == 16);
static_assert(offsetof(worr_native_command_shadow_compare_report_v1,
                       native_sample_time_us) == 32);
static_assert(offsetof(worr_native_command_shadow_compare_report_v1,
                       observed_offset_us) == 48);

static_assert(std::is_standard_layout_v<
              worr_native_command_shadow_join_slot_v1>);
static_assert(std::is_trivially_copyable_v<
              worr_native_command_shadow_join_slot_v1>);
static_assert(sizeof(worr_native_command_shadow_join_slot_v1) == 312);
static_assert(offsetof(worr_native_command_shadow_join_slot_v1,
                       native_record) == 24);
static_assert(offsetof(worr_native_command_shadow_join_slot_v1,
                       legacy_record) == 128);
static_assert(offsetof(worr_native_command_shadow_join_slot_v1,
                       comparison) == 232);

static_assert(std::is_standard_layout_v<
              worr_native_command_shadow_join_v1>);
static_assert(std::is_trivially_copyable_v<
              worr_native_command_shadow_join_v1>);
static_assert(sizeof(worr_native_command_shadow_join_v1) == 20224);
static_assert(offsetof(worr_native_command_shadow_join_v1,
                       comparator) == 32);
static_assert(offsetof(worr_native_command_shadow_join_v1,
                       telemetry) == 120);
static_assert(offsetof(worr_native_command_shadow_join_v1,
                       slots) == 256);

int main()
{
    worr_native_command_shadow_builder_v1 builder{};
    worr_native_command_shadow_payload_registry_v1 registry{};
    worr_native_command_shadow_join_v1 join{};
    return builder.struct_size == 0 && registry.struct_size == 0 &&
                   join.struct_size == 0
               ? 0
               : 1;
}
