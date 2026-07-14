#include "common/net/legacy_command_adapter.h"

int main(void)
{
    return sizeof(worr_legacy_command_adapter_report_v1) == 64 ? 0 : 1;
}
