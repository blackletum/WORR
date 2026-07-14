/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "common/net/legacy_command_adapter.h"

#ifdef __cplusplus
extern "C" {
#endif

void CL_CommandIdentityReset(uint32_t command_epoch);
void CL_CommandIdentityShutdown(void);
bool CL_CommandIdentityFinalize(uint32_t legacy_command_number);
bool CL_CommandIdentityForNumber(uint32_t legacy_command_number,
                                 worr_command_id_v1 *id_out);
bool CL_CommandIdentityGetState(uint32_t *initial_epoch_out,
                                uint32_t *baseline_legacy_sequence_out);

/* Emits exactly nine adjacent CLC_SETTING services into the supplied q2proto
 * writer.  The caller stages them with the matching move and commits the
 * combined byte range atomically. */
bool CL_CommandIdentityWriteSideband(uintptr_t write_io_arg,
                                     uint32_t first_legacy_command_number,
                                     uint32_t command_count);

#ifdef __cplusplus
}
#endif
