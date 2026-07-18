/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "shared/local_action_shadow.h"

#ifdef __cplusplus
extern "C" {
#endif

void SV_LocalActionShadowAuthorityResetMap(void);
void SV_LocalActionShadowAuthorityResetClient(uint32_t client_index);

const worr_local_action_shadow_authority_import_v1 *
SV_LocalActionShadowAuthorityImportV1(void);

/* Copies the oldest published receipt without removing it. */
bool SV_LocalActionShadowAuthorityPeekNextReceipt(
    uint32_t client_index,
    worr_local_action_shadow_authority_receipt_v1 *receipt_out);

/* Removes the oldest receipt only when it is byte-identical to expected. */
bool SV_LocalActionShadowAuthorityConsumeNextReceipt(
    uint32_t client_index,
    const worr_local_action_shadow_authority_receipt_v1 *expected);

#ifdef __cplusplus
}
#endif
