// Copyright (c) 2026 The WORR Project
// Licensed under the GNU General Public License 2.0.

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

// Temporary cross-DLL layout contract for the legacy C pmove_t and WORR C++
// PMove mirror.  Both sides assert against these values so a layout drift fails
// the build instead of silently corrupting prediction.  FR-10-T02 will replace
// the mirrored callback types with one canonical, exactly typed ABI.
namespace worr::pmove_abi_v1 {

inline constexpr std::size_t pointer_size = sizeof(void *);
static_assert(pointer_size == 4 || pointer_size == 8,
              "unsupported PMove ABI pointer size");

inline constexpr std::size_t pmove_size = pointer_size == 8 ? 3320 : 2644;
inline constexpr std::size_t pmove_alignment = pointer_size == 8 ? 8 : 4;
inline constexpr std::size_t touch_offset = 88;
inline constexpr std::size_t view_angles_offset =
    pointer_size == 8 ? 3168 : 2524;
inline constexpr std::size_t trace_callback_offset =
    pointer_size == 8 ? 3256 : 2596;
inline constexpr std::size_t clip_callback_offset =
    pointer_size == 8 ? 3264 : 2600;
inline constexpr std::size_t point_contents_callback_offset =
    pointer_size == 8 ? 3272 : 2604;
inline constexpr std::size_t impact_delta_offset =
    pointer_size == 8 ? 3312 : 2640;

inline constexpr std::size_t state_size = 52;
inline constexpr std::size_t command_size = 32;
inline constexpr std::size_t command_offset = 52;
inline constexpr std::size_t snap_initial_offset = 84;

} // namespace worr::pmove_abi_v1
