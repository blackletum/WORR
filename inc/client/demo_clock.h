/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/demo_clock_sideband.h"

#ifdef __cplusplus
extern "C" {
#endif

void CL_DemoClockReset(void);
/* Preserve only a pristine armed synthetic prefix across svc_serverdata. */
void CL_DemoClockServerDataReset(void);
void CL_DemoClockShutdown(void);

/* Arms exactly the next parsed packet as an in-memory seek snapshot. */
bool CL_DemoClockArmSyntheticPacket(void);
bool CL_DemoClockPacketBegin(void);
bool CL_DemoClockPacketEnd(void);

/* handled_out is true only for the reserved demo-clock setting range. */
bool CL_DemoClockObserveSetting(int32_t index, int32_t value,
                                bool *handled_out);
bool CL_DemoClockObserveInterveningService(void);

/* Ordinary live/demo frames succeed with present_out=false. */
bool CL_DemoClockConsumeFrame(int32_t server_frame,
                              bool *present_out,
                              uint64_t *server_time_us_out);

#ifdef __cplusplus
}
#endif
