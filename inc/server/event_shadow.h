/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "shared/event_shadow.h"

#ifdef __cplusplus
extern "C" {
#endif

void SV_EventShadowResetMap(void);
const worr_event_shadow_import_v1 *SV_EventShadowImportV1(void);

#if defined(WORR_EVENT_SHADOW_TESTING)
bool SV_EventShadowTestSetCursor(uint32_t stream_epoch, uint32_t sequence);
#endif

#ifdef __cplusplus
}
#endif

