/* Copyright (C) 2026 WORR contributors */

#pragma once

#include "shared/command_context.h"

#ifdef __cplusplus
extern "C" {
#endif

void SV_CommandContextReset(void);
bool SV_CommandContextBegin(
    const worr_authoritative_command_context_v1 *context);
bool SV_CommandContextBeginRejected(void);
void SV_CommandContextEnd(void);
const worr_command_context_import_v1 *SV_CommandContextImportV1(void);

#ifdef __cplusplus
}
#endif
