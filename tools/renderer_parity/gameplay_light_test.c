/* Renderer-neutral gameplay light query regression tests. */

#include "client/gameplay_light.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #condition); \
            return false;                                                      \
        }                                                                      \
    } while (0)

static bool NearlyEqual(float a, float b)
{
    return fabsf(a - b) < 0.0001f;
}

static lightpoint_t world_lightpoint;
static lightpoint_t bmodel_lightpoint;
static int transformed_lightpoint_calls;
static const lightgrid_t *test_grid;
static lightgrid_sample_t test_grid_samples[2];

void BSP_LightPoint(lightpoint_t *point, const vec3_t start, const vec3_t end,
                    const mnode_t *headnode, int nolm_mask)
{
    (void)start;
    (void)end;
    (void)headnode;
    (void)nolm_mask;
    *point = world_lightpoint;
}

void BSP_TransformedLightPoint(lightpoint_t *point, const vec3_t start,
                               const vec3_t end, const mnode_t *headnode,
                               int nolm_mask, const vec3_t origin,
                               const vec3_t angles)
{
    (void)start;
    (void)end;
    (void)headnode;
    (void)nolm_mask;
    (void)origin;
    (void)angles;
    transformed_lightpoint_calls++;
    *point = bmodel_lightpoint;
}

const lightgrid_sample_t *BSP_LookupLightgrid(const lightgrid_t *grid,
                                               const uint32_t point[3])
{
    if (grid != test_grid || point[1] != 0 || point[2] != 0 || point[0] > 1) {
        return NULL;
    }
    return &test_grid_samples[point[0]];
}

static bool TestFaceSamplingBoundaries(void)
{
    lightstyle_t styles[2] = {{0}};
    styles[0].white = 1.0f;
    styles[1].white = 0.5f;

    const byte lightmap[] = {
        0, 0, 0,      100, 0, 0,
        200, 0, 0,    255, 0, 0,
        0, 0, 40,     0, 0, 40,
        0, 0, 40,     0, 0, 40,
    };
    mface_t face = {0};
    face.lightmap = (byte *)lightmap;
    face.lm_width = 2;
    face.lm_height = 2;
    face.numstyles = 2;
    face.styles[0] = 0;
    face.styles[1] = 1;

    vec3_t color;
    CHECK(GameplayLight_SampleFace(&face, 0.5f, 0.5f, styles, 2, color));
    CHECK(NearlyEqual(color[0], 138.75f / 255.0f));
    CHECK(NearlyEqual(color[1], 0.0f));
    CHECK(NearlyEqual(color[2], 20.0f / 255.0f));

    CHECK(GameplayLight_SampleFace(&face, -FLT_MAX, FLT_MAX, styles, 2, color));
    CHECK(NearlyEqual(color[0], 200.0f / 255.0f));
    CHECK(NearlyEqual(color[2], 20.0f / 255.0f));
    return true;
}

static bool TestOneDimensionalFaceSampling(void)
{
    lightstyle_t styles[1] = {{0}};
    styles[0].white = 1.0f;
    vec3_t color;

    const byte vertical_lightmap[] = {
        0, 0, 0,
        0, 100, 0,
        0, 200, 0,
    };
    mface_t vertical = {0};
    vertical.lightmap = (byte *)vertical_lightmap;
    vertical.lm_width = 1;
    vertical.lm_height = 3;
    vertical.numstyles = 1;
    vertical.styles[0] = 0;
    CHECK(GameplayLight_SampleFace(&vertical, NAN, 1.5f, styles, 1, color));
    CHECK(NearlyEqual(color[0], 0.0f));
    CHECK(NearlyEqual(color[1], 150.0f / 255.0f));

    const byte horizontal_lightmap[] = {
        0, 0, 0,
        0, 0, 100,
        0, 0, 200,
    };
    mface_t horizontal = {0};
    horizontal.lightmap = (byte *)horizontal_lightmap;
    horizontal.lm_width = 3;
    horizontal.lm_height = 1;
    horizontal.numstyles = 1;
    horizontal.styles[0] = 0;
    CHECK(GameplayLight_SampleFace(&horizontal, 1.5f, INFINITY, styles, 1,
                                   color));
    CHECK(NearlyEqual(color[1], 0.0f));
    CHECK(NearlyEqual(color[2], 150.0f / 255.0f));
    return true;
}

static bool TestMissingStyleTerminator(void)
{
    lightstyle_t styles[1] = {{0}};
    styles[0].white = 1.0f;
    const byte lightmap[] = {64, 32, 16};
    mface_t face = {0};
    vec3_t color;

    face.lightmap = (byte *)lightmap;
    face.lm_width = 1;
    face.lm_height = 1;
    face.numstyles = 2;
    face.styles[0] = 0;
    face.styles[1] = 255;
    CHECK(GameplayLight_SampleFace(&face, 0.0f, 0.0f, styles, 1, color));
    CHECK(NearlyEqual(color[0], 64.0f / 255.0f));
    CHECK(NearlyEqual(color[1], 32.0f / 255.0f));
    CHECK(NearlyEqual(color[2], 16.0f / 255.0f));
    return true;
}

static bool TestGridAndBrushModelQueries(void)
{
    lightstyle_t styles[1] = {{0}};
    styles[0].white = 1.0f;
    vec3_t origin = {0.5f, 0.0f, 0.0f};
    vec3_t color;
    bsp_t bsp = {0};
    gameplay_light_query_t query = {0};

    bsp.lightgrid.numleafs = 1;
    bsp.lightgrid.numstyles = 1;
    bsp.lightgrid.scale[0] = 1.0f;
    bsp.lightgrid.scale[1] = 1.0f;
    bsp.lightgrid.scale[2] = 1.0f;
    bsp.lightgrid.size[0] = 2;
    bsp.lightgrid.size[1] = 1;
    bsp.lightgrid.size[2] = 1;
    test_grid = &bsp.lightgrid;
    test_grid_samples[0] = (lightgrid_sample_t){.style = 0, .rgb = {0, 0, 0}};
    test_grid_samples[1] = (lightgrid_sample_t){.style = 0, .rgb = {200, 0, 0}};
    query.bsp = &bsp;
    query.lightstyles = styles;
    query.num_lightstyles = 1;
    CHECK(GameplayLight_Query(&query, origin, color));
    CHECK(NearlyEqual(color[0], 100.0f / 255.0f));
    CHECK(NearlyEqual(color[1], 0.0f));

    const byte world_map[] = {0, 0, 64};
    const byte bmodel_map[] = {128, 0, 0};
    mface_t world_face = {0};
    mface_t bmodel_face = {0};
    mnode_t dummy_node = {0};
    mmodel_t models[2] = {0};
    entity_t entity = {0};
    test_grid = NULL;
    bsp.lightgrid.numleafs = 0;
    bsp.lightmap = (byte *)world_map;
    bsp.nodes = &dummy_node;
    bsp.models = models;
    bsp.nummodels = 2;
    models[1].numfaces = 1;
    models[1].headnode = &dummy_node;
    VectorSet(models[1].mins, -8.0f, -8.0f, -8.0f);
    VectorSet(models[1].maxs, 8.0f, 8.0f, 8.0f);

    world_face.lightmap = (byte *)world_map;
    world_face.lm_width = world_face.lm_height = 1;
    world_face.numstyles = 1;
    world_face.styles[0] = 0;
    bmodel_face.lightmap = (byte *)bmodel_map;
    bmodel_face.lm_width = bmodel_face.lm_height = 1;
    bmodel_face.numstyles = 1;
    bmodel_face.styles[0] = 0;
    world_lightpoint = (lightpoint_t){.surf = &world_face, .fraction = 0.5f};
    bmodel_lightpoint = (lightpoint_t){.surf = &bmodel_face, .fraction = 0.25f};
    entity.model = ~1;
    query.entities = &entity;
    query.num_entities = 1;
    transformed_lightpoint_calls = 0;
    CHECK(GameplayLight_Query(&query, origin, color));
    CHECK(transformed_lightpoint_calls == 1);
    CHECK(NearlyEqual(color[0], 128.0f / 255.0f));
    CHECK(NearlyEqual(color[2], 0.0f));

    query.bsp = NULL;
    CHECK(!GameplayLight_Query(&query, origin, color));
    CHECK(NearlyEqual(color[0], 1.0f));
    CHECK(NearlyEqual(color[1], 1.0f));
    CHECK(NearlyEqual(color[2], 1.0f));
    return true;
}

static bool TestProtocolByteConversion(void)
{
    vec3_t color = {-0.5f, -0.25f, -0.125f};
    CHECK(GameplayLight_LevelToByte(color) == 0);
    VectorSet(color, 0.25f, 0.5f, 0.125f);
    CHECK(GameplayLight_LevelToByte(color) == 75);
    VectorSet(color, 2.0f, 0.0f, 0.0f);
    CHECK(GameplayLight_LevelToByte(color) == 255);
    VectorSet(color, NAN, 1.0f, 1.0f);
    CHECK(GameplayLight_LevelToByte(color) == 0);
    VectorSet(color, INFINITY, 0.0f, 0.0f);
    CHECK(GameplayLight_LevelToByte(color) == 255);
    VectorSet(color, -INFINITY, 0.0f, 0.0f);
    CHECK(GameplayLight_LevelToByte(color) == 0);
    return true;
}

int main(void)
{
    if (!TestFaceSamplingBoundaries() || !TestOneDimensionalFaceSampling() ||
        !TestMissingStyleTerminator() || !TestGridAndBrushModelQueries() ||
        !TestProtocolByteConversion()) {
        return 1;
    }

    puts("gameplay_light_test: all checks passed");
    return 0;
}
