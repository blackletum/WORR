#include "shared/command_context.h"

#include <stddef.h>

_Static_assert(sizeof(worr_authoritative_command_context_v1) == 256,
               "command context size");
_Static_assert(sizeof(worr_command_context_import_v1) == 24,
               "command context import size");
_Static_assert(offsetof(worr_command_context_import_v1, GetScopeState) == 16,
               "command scope callback offset");
_Static_assert(WORR_COMMAND_CONTEXT_SCOPE_INACTIVE_LEGACY == 0,
               "legacy scope value");
_Static_assert(WORR_COMMAND_CONTEXT_SCOPE_ACTIVE_VALID == 1,
               "valid scope value");
_Static_assert(WORR_COMMAND_CONTEXT_SCOPE_ACTIVE_REJECTED == 2,
               "rejected scope value");

int main(void)
{
    return 0;
}
