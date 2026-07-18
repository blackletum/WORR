/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "shared/local_action_shadow.h"

/*
 * Maps opaque sgame item identities to the frozen shared catalog and verifies
 * the exact active-ammo binding before constructing descriptor-complete
 * shadow evidence. It does not infer emissions or call weapon code.
 */
bool SG_LocalActionBuildShadowFromObservation(
    const worr_local_action_observation_record_v1 *observation,
    worr_local_action_shadow_v1 *shadow_out);
