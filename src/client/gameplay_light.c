/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "client/gameplay_light.h"

#include <math.h>

#define GAMEPLAY_LIGHT_FALLBACK 1.0f

static float GameplayLight_StyleScale(const lightstyle_t *lightstyles,
                                      int num_lightstyles, byte style)
{
    if (!lightstyles || style >= num_lightstyles) {
        return 1.0f;
    }

    const float scale = lightstyles[style].white;
    return isfinite(scale) ? scale : 0.0f;
}

static void GameplayLight_BytesToNormalized(const byte rgb[3], vec3_t out_color)
{
    out_color[0] = rgb[0] * (1.0f / 255.0f);
    out_color[1] = rgb[1] * (1.0f / 255.0f);
    out_color[2] = rgb[2] * (1.0f / 255.0f);
}

static float GameplayLight_ClampCoordinate(float coordinate, int size)
{
    if (!isfinite(coordinate)) {
        return 0.0f;
    }
    if (coordinate <= 0.0f) {
        return 0.0f;
    }

    const float max_coordinate = (float)(size - 1);
    return coordinate >= max_coordinate ? max_coordinate : coordinate;
}

static void GameplayLight_Lerp(const vec3_t a, const vec3_t b, float t,
                               vec3_t out_color)
{
    out_color[0] = a[0] + (b[0] - a[0]) * t;
    out_color[1] = a[1] + (b[1] - a[1]) * t;
    out_color[2] = a[2] + (b[2] - a[2]) * t;
}

bool GameplayLight_SampleFace(const mface_t *face, float s, float t,
                              const lightstyle_t *lightstyles,
                              int num_lightstyles, vec3_t out_color)
{
    if (!face || !face->lightmap || !out_color || face->lm_width < 1 ||
        face->lm_height < 1) {
        return false;
    }

    const int width = face->lm_width;
    const int height = face->lm_height;
    const float sample_s = GameplayLight_ClampCoordinate(s, width);
    const float sample_t = GameplayLight_ClampCoordinate(t, height);
    const int s0 = (int)floorf(sample_s);
    const int t0 = (int)floorf(sample_t);
    const int s1 = s0 + 1 < width ? s0 + 1 : s0;
    const int t1 = t0 + 1 < height ? t0 + 1 : t0;
    const float frac_s = sample_s - (float)s0;
    const float frac_t = sample_t - (float)t0;
    const size_t style_bytes = (size_t)width * (size_t)height * 3u;
    const int max_styles = (int)(sizeof(face->styles) / sizeof(face->styles[0]));
    const int style_count = face->numstyles < max_styles ? face->numstyles : max_styles;
    const byte *lightmap = face->lightmap;

    VectorClear(out_color);
    for (int style_index = 0; style_index < style_count; style_index++) {
        if (face->styles[style_index] == 255) {
            break;
        }

        const byte *b1 = &lightmap[3 * (t0 * width + s0)];
        const byte *b2 = &lightmap[3 * (t0 * width + s1)];
        const byte *b3 = &lightmap[3 * (t1 * width + s1)];
        const byte *b4 = &lightmap[3 * (t1 * width + s0)];
        vec3_t c1, c2, c3, c4, row0, row1, sample;

        GameplayLight_BytesToNormalized(b1, c1);
        GameplayLight_BytesToNormalized(b2, c2);
        GameplayLight_BytesToNormalized(b3, c3);
        GameplayLight_BytesToNormalized(b4, c4);
        GameplayLight_Lerp(c1, c2, frac_s, row0);
        GameplayLight_Lerp(c4, c3, frac_s, row1);
        GameplayLight_Lerp(row0, row1, frac_t, sample);
        VectorMA(out_color,
                 GameplayLight_StyleScale(lightstyles, num_lightstyles,
                                           face->styles[style_index]),
                 sample, out_color);
        lightmap += style_bytes;
    }

    return true;
}

bool GameplayLight_SampleGrid(const lightgrid_t *grid, const vec3_t origin,
                              const lightstyle_t *lightstyles,
                              int num_lightstyles, vec3_t out_color)
{
    if (!grid || !origin || !out_color || !grid->numleafs) {
        return false;
    }

    vec3_t point;
    uint32_t point_i[3];
    uint32_t point_next[3];
    for (int axis = 0; axis < 3; axis++) {
        point[axis] = (origin[axis] - grid->mins[axis]) * grid->scale[axis];
        if (!isfinite(point[axis]) || !grid->size[axis] || point[axis] < 0.0f ||
            point[axis] > (float)(grid->size[axis] - 1)) {
            return false;
        }
        point_i[axis] = (uint32_t)floorf(point[axis]);
        point_next[axis] = point_i[axis] + 1 < grid->size[axis]
                               ? point_i[axis] + 1
                               : point_i[axis];
    }

    vec3_t samples[8], average;
    int sample_mask = 0;
    int sample_count = 0;
    VectorClear(average);

    for (int corner = 0; corner < 8; corner++) {
        const uint32_t sample_point[3] = {
            (corner & BIT(0)) ? point_next[0] : point_i[0],
            (corner & BIT(1)) ? point_next[1] : point_i[1],
            (corner & BIT(2)) ? point_next[2] : point_i[2],
        };
        const lightgrid_sample_t *sample = BSP_LookupLightgrid(grid, sample_point);
        if (!sample) {
            continue;
        }

        VectorClear(samples[corner]);
        int style_count = 0;
        for (; style_count < (int)grid->numstyles && sample->style != 255;
             style_count++, sample++) {
            vec3_t rgb;
            GameplayLight_BytesToNormalized(sample->rgb, rgb);
            VectorMA(samples[corner],
                     GameplayLight_StyleScale(lightstyles, num_lightstyles,
                                               sample->style),
                     rgb, samples[corner]);
        }

        if (style_count > 0) {
            sample_mask |= BIT(corner);
            VectorAdd(average, samples[corner], average);
            sample_count++;
        }
    }

    if (!sample_mask) {
        return false;
    }

    if (sample_mask != 255) {
        VectorScale(average, 1.0f / (float)sample_count, average);
        for (int corner = 0; corner < 8; corner++) {
            if (!(sample_mask & BIT(corner))) {
                VectorCopy(average, samples[corner]);
            }
        }
    }

    const float fx = point[0] - (float)point_i[0];
    const float fy = point[1] - (float)point_i[1];
    const float fz = point[2] - (float)point_i[2];
    vec3_t lerp_x[4], lerp_y[2];

    GameplayLight_Lerp(samples[0], samples[1], fx, lerp_x[0]);
    GameplayLight_Lerp(samples[2], samples[3], fx, lerp_x[1]);
    GameplayLight_Lerp(samples[4], samples[5], fx, lerp_x[2]);
    GameplayLight_Lerp(samples[6], samples[7], fx, lerp_x[3]);
    GameplayLight_Lerp(lerp_x[0], lerp_x[1], fy, lerp_y[0]);
    GameplayLight_Lerp(lerp_x[2], lerp_x[3], fy, lerp_y[1]);
    GameplayLight_Lerp(lerp_y[0], lerp_y[1], fz, out_color);
    return true;
}

static bool GameplayLight_HasFiniteOrigin(const vec3_t origin)
{
    return origin && isfinite(origin[0]) && isfinite(origin[1]) &&
           isfinite(origin[2]);
}

bool GameplayLight_Query(const gameplay_light_query_t *query,
                         const vec3_t origin, vec3_t out_color)
{
    if (!out_color) {
        return false;
    }

    VectorSet(out_color, GAMEPLAY_LIGHT_FALLBACK, GAMEPLAY_LIGHT_FALLBACK,
              GAMEPLAY_LIGHT_FALLBACK);
    if (!query || !query->bsp || !GameplayLight_HasFiniteOrigin(origin)) {
        return false;
    }

    const bsp_t *bsp = query->bsp;
    if (GameplayLight_SampleGrid(&bsp->lightgrid, origin, query->lightstyles,
                                 query->num_lightstyles, out_color)) {
        return true;
    }

    if (!bsp->lightmap || !bsp->nodes) {
        return false;
    }

    vec3_t end = {origin[0], origin[1], origin[2] - 8192.0f};
    if (!isfinite(end[2])) {
        return false;
    }

    const int nolm_mask = (bsp->has_bspx ? SURF_NOLM_MASK_REMASTER :
                                            SURF_NOLM_MASK_DEFAULT) |
                          SURF_TRANS_MASK;
    lightpoint_t lightpoint;
    BSP_LightPoint(&lightpoint, origin, end, bsp->nodes, nolm_mask);

    for (int entity_index = 0;
         query->entities && entity_index < query->num_entities; entity_index++) {
        const entity_t *entity = &query->entities[entity_index];
        if (!(entity->model & BIT(31))) {
            continue;
        }

        const int model_index = ~entity->model;
        if (!bsp->models || model_index < 1 || model_index >= bsp->nummodels) {
            continue;
        }

        const mmodel_t *model = &bsp->models[model_index];
        if (!model->numfaces || !model->headnode) {
            continue;
        }

        const vec_t *angles;
        if (!VectorEmpty(entity->angles)) {
            if (fabsf(origin[0] - entity->origin[0]) > model->radius ||
                fabsf(origin[1] - entity->origin[1]) > model->radius) {
                continue;
            }
            angles = entity->angles;
        } else {
            vec3_t mins, maxs;
            VectorAdd(model->mins, entity->origin, mins);
            VectorAdd(model->maxs, entity->origin, maxs);
            if (origin[0] < mins[0] || origin[0] > maxs[0] ||
                origin[1] < mins[1] || origin[1] > maxs[1]) {
                continue;
            }
            angles = NULL;
        }

        lightpoint_t transformed;
        BSP_TransformedLightPoint(&transformed, origin, end, model->headnode,
                                  nolm_mask, entity->origin, angles);
        if (transformed.fraction < lightpoint.fraction) {
            lightpoint = transformed;
        }
    }

    if (!lightpoint.surf ||
        !GameplayLight_SampleFace(lightpoint.surf, lightpoint.s, lightpoint.t,
                                  query->lightstyles, query->num_lightstyles,
                                  out_color)) {
        VectorSet(out_color, GAMEPLAY_LIGHT_FALLBACK, GAMEPLAY_LIGHT_FALLBACK,
                  GAMEPLAY_LIGHT_FALLBACK);
        return false;
    }

    return true;
}

int GameplayLight_LevelToByte(const vec3_t color)
{
    if (!color) {
        return 0;
    }

    float brightest = 0.0f;
    for (int channel = 0; channel < 3; channel++) {
        if (isnan(color[channel])) {
            return 0;
        }
        if (color[channel] > brightest) {
            brightest = color[channel];
        }
    }

    const float level = 150.0f * brightest;
    if (level <= 0.0f) {
        return 0;
    }
    if (level >= 255.0f) {
        return 255;
    }
    return (int)level;
}
