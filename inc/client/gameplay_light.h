/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

/*
 * Renderer-neutral static-light query used for the gameplay lightlevel byte.
 *
 * The result is authored BSP light data in the 0..1 domain, multiplied only
 * by the current map lightstyles.  It deliberately excludes renderer
 * overbright, gamma, intensity, brightness/modulate, fullbright and dynamic
 * lights so a client's presentation settings cannot affect gameplay state.
 */

#include "common/bsp.h"
#include "renderer/renderer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const bsp_t        *bsp;
    const lightstyle_t *lightstyles;
    int                 num_lightstyles;
    const entity_t     *entities;
    int                 num_entities;
} gameplay_light_query_t;

/* Raw authored BSP light samples; no renderer adjustments are applied. */
bool GameplayLight_SampleFace(const mface_t *face, float s, float t,
                              const lightstyle_t *lightstyles,
                              int num_lightstyles, vec3_t out_color);
bool GameplayLight_SampleGrid(const lightgrid_t *grid, const vec3_t origin,
                              const lightstyle_t *lightstyles,
                              int num_lightstyles, vec3_t out_color);

/* Returns true when a static BSP sample was found.  On a miss, out_color is
 * the stable white fallback retained for legacy gameplay lightlevel behavior.
 */
bool GameplayLight_Query(const gameplay_light_query_t *query,
                         const vec3_t origin, vec3_t out_color);

/* Converts a static-light RGB triplet to the uint8-compatible gameplay byte.
 * NaN is rejected, negative values clamp to zero and positive infinity (or
 * values above the protocol range) saturate to 255.
 */
int GameplayLight_LevelToByte(const vec3_t color);

#ifdef __cplusplus
}
#endif
