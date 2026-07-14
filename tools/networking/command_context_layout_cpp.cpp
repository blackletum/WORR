#include "shared/command_context.h"

#include <cstddef>
#include <type_traits>

static_assert(
    std::is_standard_layout_v<worr_authoritative_command_context_v1>);
static_assert(
    std::is_trivially_copyable_v<worr_authoritative_command_context_v1>);
static_assert(std::is_standard_layout_v<worr_command_context_import_v1>);
static_assert(sizeof(worr_authoritative_command_context_v1) == 256);
static_assert(sizeof(worr_command_context_import_v1) == 24);
static_assert(offsetof(worr_command_context_import_v1, GetScopeState) == 16);

int main()
{
    return 0;
}
