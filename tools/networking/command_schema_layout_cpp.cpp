/* Compile the public command schema and stream declarations as C++20. */
#include "common/net/command_stream.h"

#include <type_traits>

static_assert(std::is_standard_layout_v<worr_command_id_v1>);
static_assert(std::is_trivially_copyable_v<worr_command_id_v1>);
static_assert(std::is_standard_layout_v<worr_command_cursor_v1>);
static_assert(std::is_trivially_copyable_v<worr_command_cursor_v1>);
static_assert(std::is_standard_layout_v<worr_command_render_watermark_v1>);
static_assert(std::is_trivially_copyable_v<worr_command_render_watermark_v1>);
static_assert(std::is_standard_layout_v<worr_command_record_v1>);
static_assert(std::is_trivially_copyable_v<worr_command_record_v1>);
static_assert(std::is_standard_layout_v<worr_command_stream_slot_v1>);
static_assert(std::is_trivially_copyable_v<worr_command_stream_slot_v1>);

int main()
{
    return WORR_COMMAND_ABI_VERSION == 1 &&
                   WORR_COMMAND_STREAM_VERSION == 1
               ? 0
               : 1;
}
