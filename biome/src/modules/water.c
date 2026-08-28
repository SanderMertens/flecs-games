#define BIOME_WATER_IMPL

#include "biome.h"
#include "weather_model.h"
#include "water_shader.h"

typedef struct WaterMeshState {
    ecs_entity_t asset;
    ecs_entity_t instance;
    ecs_entity_t shader;
    float frozen;
    bool visible;
} WaterMeshState;

ECS_COMPONENT_DECLARE(WaterMeshState);

typedef struct WaterRenderState {
    ecs_entity_t shader;
} WaterRenderState;

ECS_COMPONENT_DECLARE(WaterRenderState);

static bool biomeWaterTerrainCellSubmerged(
    const float *height,
    int32_t stride,
    int32_t x,
    int32_t z,
    float water_level)
{
    int32_t i = z * stride + x;
    return height[i] < water_level ||
        height[i + 1] < water_level ||
        height[i + stride] < water_level ||
        height[i + stride + 1] < water_level;
}

static float biomeWaterTerrainCellHeight(
    const float *height,
    int32_t stride,
    int32_t x,
    int32_t z)
{
    int32_t i = z * stride + x;
    return 0.25f * (
        height[i] + height[i + 1] +
        height[i + stride] + height[i + stride + 1]);
}

static float biomeWaterTerrainCellDepth(
    const float *height,
    int32_t stride,
    int32_t x,
    int32_t z,
    float water_level)
{
    float result = water_level - biomeWaterTerrainCellHeight(
        height, stride, x, z);
    return result > 0 ? result : 0;
}

static float biomeWaterFrozen(float temperature) {
    float result = -temperature / 10.0f;
    if (result < 0) {
        return 0;
    }
    if (result > 1) {
        return 1;
    }
    return result;
}

static void biomeWaterEmitSide(
    flecs_vec3_t *vertices,
    flecs_vec3_t *normals,
    flecs_vec2_t *uvs,
    uint32_t *indices,
    int32_t *vertex_index,
    int32_t *index,
    flecs_vec3_t top_a,
    flecs_vec3_t top_b,
    float bottom_a_height,
    float bottom_b_height,
    flecs_vec3_t normal,
    float water_depth,
    float frozen)
{
    int32_t v = *vertex_index;
    if (bottom_a_height > top_a.y) {
        bottom_a_height = top_a.y;
    }
    if (bottom_b_height > top_b.y) {
        bottom_b_height = top_b.y;
    }
    flecs_vec3_t bottom_a = {top_a.x, bottom_a_height, top_a.z};
    flecs_vec3_t bottom_b = {top_b.x, bottom_b_height, top_b.z};
    vertices[v] = top_a;
    vertices[v + 1] = top_b;
    vertices[v + 2] = bottom_b;
    vertices[v + 3] = bottom_a;
    for (int32_t i = 0; i < 4; i ++) {
        normals[v + i] = normal;
        uvs[v + i] = (flecs_vec2_t){water_depth, frozen};
    }

    flecs_vec3_t ab = {
        top_b.x - top_a.x,
        top_b.y - top_a.y,
        top_b.z - top_a.z
    };
    flecs_vec3_t ac = {
        bottom_b.x - top_a.x,
        bottom_b.y - top_a.y,
        bottom_b.z - top_a.z
    };
    flecs_vec3_t face_normal = {
        ab.y * ac.z - ab.z * ac.y,
        ab.z * ac.x - ab.x * ac.z,
        ab.x * ac.y - ab.y * ac.x
    };
    bool reverse =
        face_normal.x * normal.x +
        face_normal.y * normal.y +
        face_normal.z * normal.z < 0;
    indices[*index] = (uint32_t)v;
    indices[*index + 1] = (uint32_t)(v + (reverse ? 2 : 1));
    indices[*index + 2] = (uint32_t)(v + (reverse ? 1 : 2));
    indices[*index + 3] = (uint32_t)v;
    indices[*index + 4] = (uint32_t)(v + (reverse ? 3 : 2));
    indices[*index + 5] = (uint32_t)(v + (reverse ? 2 : 3));
    *vertex_index += 4;
    *index += 6;
}

static void biomeWaterEnsureAsset(
    ecs_world_t *world,
    ecs_entity_t terrain,
    WaterMeshState *state)
{
    if (!state->asset) {
        state->asset = ecs_entity(world, {
            .name = "water_mesh",
            .parent = terrain,
            .add = ecs_ids(EcsPrefab)
        });
    }
}

static void biomeWaterEnsureInstance(
    ecs_world_t *world,
    ecs_entity_t terrain,
    WaterMeshState *state,
    ecs_entity_t shader)
{
    if (!state->instance) {
        state->instance = ecs_entity(world, {
            .name = "water",
            .parent = terrain
        });
        ecs_add_pair(world, state->instance, EcsIsA, state->asset);
        ecs_set(world, state->instance, FlecsPosition3, {0, 0, 0});
        ecs_set(world, state->instance, FlecsRgba, {30, 100, 170, 255});
        ecs_set(world, state->instance, FlecsPbrMaterial, {
            .metallic = 0,
            .roughness = 0.08f
        });
        ecs_set(world, state->instance, FlecsTransmission, {
            .transmission_factor = 0.82f,
            .ior = 1.333f,
            .thickness_factor = 0.5f,
            .attenuation_distance = 5.0f,
            .attenuation_color = {20, 100, 190, 255}
        });
        ecs_add_pair(
            world, state->instance, FlecsCustomShader, shader);
        ecs_add_id(world, state->instance, FlecsDynamicTransform);
        state->shader = shader;
        state->visible = true;
    } else if (state->shader != shader) {
        if (state->shader) {
            ecs_remove_pair(
                world, state->instance, FlecsCustomShader, state->shader);
        }
        ecs_add_pair(
            world, state->instance, FlecsCustomShader, shader);
        state->shader = shader;
    } else {
        ecs_modified(world, state->instance, FlecsPosition3);
    }
}

static void biomeWaterSetVisible(
    ecs_world_t *world,
    WaterMeshState *state,
    bool visible)
{
    if (!state->instance || state->visible == visible) {
        return;
    }
    ecs_enable(world, state->instance, visible);
    state->visible = visible;
}

static void biomeWaterUpdateFrozen(
    ecs_world_t *world,
    WaterMeshState *state,
    float frozen)
{
    if (!state->asset || state->frozen == frozen) {
        return;
    }
    state->frozen = frozen;
    FlecsMesh3 *mesh = ecs_get_mut(world, state->asset, FlecsMesh3);
    if (!mesh || !ecs_vec_count(&mesh->indices)) {
        return;
    }
    int32_t count = ecs_vec_count(&mesh->uvs);
    flecs_vec2_t *uvs = ecs_vec_first_t(&mesh->uvs, flecs_vec2_t);
    for (int32_t i = 0; i < count; i ++) {
        uvs[i].y = frozen;
    }
    flecsEngine_mesh3_updateVertices(world, state->asset);
}

static void biomeWaterBuildMesh(
    ecs_world_t *world,
    ecs_entity_t terrain_entity,
    const FlecsTerrain *terrain,
    float water_level,
    float frozen,
    WaterMeshState *state,
    ecs_entity_t shader)
{
    bool rebuild = state->asset != 0;
    int32_t width = terrain->width;
    int32_t depth = terrain->depth;
    int32_t stride = width + 1;
    int32_t corner_count = stride * (depth + 1);
    const float *height = ecs_vec_first_t(&terrain->heights, float);
    int32_t wet_count = 0;
    int32_t edge_count = 0;

    for (int32_t z = 0; z < depth; z ++) {
        for (int32_t x = 0; x < width; x ++) {
            wet_count += biomeWaterTerrainCellSubmerged(
                height, stride, x, z, water_level);
        }
    }
    for (int32_t x = 0; x < width; x ++) {
        edge_count += biomeWaterTerrainCellSubmerged(
            height, stride, x, 0, water_level);
        edge_count += biomeWaterTerrainCellSubmerged(
            height, stride, x, depth - 1, water_level);
    }
    for (int32_t z = 0; z < depth; z ++) {
        edge_count += biomeWaterTerrainCellSubmerged(
            height, stride, 0, z, water_level);
        edge_count += biomeWaterTerrainCellSubmerged(
            height, stride, width - 1, z, water_level);
    }

    biomeWaterEnsureAsset(world, terrain_entity, state);
    FlecsMesh3 *mesh = ecs_ensure(world, state->asset, FlecsMesh3);
    ecs_vec_set_count_t(
        NULL, &mesh->vertices, flecs_vec3_t,
        corner_count + edge_count * 4);
    ecs_vec_set_count_t(
        NULL, &mesh->normals, flecs_vec3_t,
        corner_count + edge_count * 4);
    ecs_vec_set_count_t(
        NULL, &mesh->uvs, flecs_vec2_t,
        corner_count + edge_count * 4);
    ecs_vec_set_count_t(
        NULL, &mesh->indices, uint32_t,
        (wet_count + edge_count) * 6);
    ecs_vec_set_count_t(NULL, &mesh->colors, flecs_rgba_t, 0);

    flecs_vec3_t *vertices = ecs_vec_first_t(
        &mesh->vertices, flecs_vec3_t);
    flecs_vec3_t *normals = ecs_vec_first_t(
        &mesh->normals, flecs_vec3_t);
    flecs_vec2_t *uvs = ecs_vec_first_t(
        &mesh->uvs, flecs_vec2_t);
    uint32_t *indices = ecs_vec_first_t(
        &mesh->indices, uint32_t);

    for (int32_t z = 0; z <= depth; z ++) {
        for (int32_t x = 0; x <= width; x ++) {
            int32_t i = z * stride + x;
            vertices[i] = (flecs_vec3_t){
                (float)x * TerrainCellSize,
                water_level,
                (float)z * TerrainCellSize
            };
            normals[i] = (flecs_vec3_t){0, 1, 0};
            uvs[i] = (flecs_vec2_t){water_level - height[i], frozen};
        }
    }

    int32_t index = 0;
    for (int32_t z = 0; z < depth; z ++) {
        for (int32_t x = 0; x < width; x ++) {
            if (!biomeWaterTerrainCellSubmerged(
                height, stride, x, z, water_level))
            {
                continue;
            }
            uint32_t i00 = (uint32_t)(z * stride + x);
            uint32_t i10 = i00 + 1;
            uint32_t i01 = i00 + (uint32_t)stride;
            uint32_t i11 = i01 + 1;
            if (fabsf(height[i00] - height[i11]) <=
                fabsf(height[i10] - height[i01]))
            {
                indices[index ++] = i00;
                indices[index ++] = i11;
                indices[index ++] = i10;
                indices[index ++] = i00;
                indices[index ++] = i01;
                indices[index ++] = i11;
            } else {
                indices[index ++] = i00;
                indices[index ++] = i01;
                indices[index ++] = i10;
                indices[index ++] = i10;
                indices[index ++] = i01;
                indices[index ++] = i11;
            }
        }
    }

    int32_t vertex_index = corner_count;
    for (int32_t x = 0; x < width; x ++) {
        if (biomeWaterTerrainCellSubmerged(
            height, stride, x, 0, water_level))
        {
            int32_t a = x;
            int32_t b = x + 1;
            biomeWaterEmitSide(
                vertices, normals, uvs, indices,
                &vertex_index, &index,
                vertices[a], vertices[b], height[a], height[b],
                (flecs_vec3_t){0, 0, -1},
                biomeWaterTerrainCellDepth(
                    height, stride, x, 0, water_level), frozen);
        }
        if (biomeWaterTerrainCellSubmerged(
            height, stride, x, depth - 1, water_level))
        {
            int32_t a = depth * stride + x;
            int32_t b = a + 1;
            biomeWaterEmitSide(
                vertices, normals, uvs, indices,
                &vertex_index, &index,
                vertices[a], vertices[b], height[a], height[b],
                (flecs_vec3_t){0, 0, 1},
                biomeWaterTerrainCellDepth(
                    height, stride, x, depth - 1, water_level), frozen);
        }
    }
    for (int32_t z = 0; z < depth; z ++) {
        if (biomeWaterTerrainCellSubmerged(
            height, stride, 0, z, water_level))
        {
            int32_t a = z * stride;
            int32_t b = (z + 1) * stride;
            biomeWaterEmitSide(
                vertices, normals, uvs, indices,
                &vertex_index, &index,
                vertices[a], vertices[b], height[a], height[b],
                (flecs_vec3_t){-1, 0, 0},
                biomeWaterTerrainCellDepth(
                    height, stride, 0, z, water_level), frozen);
        }
        if (biomeWaterTerrainCellSubmerged(
            height, stride, width - 1, z, water_level))
        {
            int32_t a = z * stride + width;
            int32_t b = (z + 1) * stride + width;
            biomeWaterEmitSide(
                vertices, normals, uvs, indices,
                &vertex_index, &index,
                vertices[a], vertices[b], height[a], height[b],
                (flecs_vec3_t){1, 0, 0},
                biomeWaterTerrainCellDepth(
                    height, stride, width - 1, z, water_level), frozen);
        }
    }

    bool was_deferred = ecs_is_deferred(world);
    if (was_deferred && rebuild) {
        ecs_defer_suspend(world);
    }
    ecs_modified(world, state->asset, FlecsMesh3);
    if (was_deferred && rebuild) {
        ecs_defer_resume(world);
    }
    state->frozen = frozen;
    biomeWaterEnsureInstance(world, terrain_entity, state, shader);
}

static void WaterUpdateMesh(ecs_iter_t *it) {
    ecs_world_t *world = it->world;
    const FlecsTerrain *terrain = ecs_field(it, FlecsTerrain, 0);
    const Weather *weather = ecs_singleton_get(world, Weather);
    const WaterRenderState *render = ecs_singleton_get(
        world, WaterRenderState);
    float frozen = weather ? biomeWaterFrozen(weather->temperature) : 0;

    if (!render) {
        return;
    }

    for (int32_t i = 0; i < it->count; i ++) {
        const Terrain *config = ecs_get(world, it->entities[i], Terrain);
        WaterMeshState *state = ecs_get_mut(
            world, it->entities[i], WaterMeshState);
        int32_t width = terrain[i].width;
        int32_t depth = terrain[i].depth;
        if (!config || !state || width <= 0 || depth <= 0 ||
            ecs_vec_count(&terrain[i].heights) !=
                (width + 1) * (depth + 1))
        {
            continue;
        }
        biomeWaterBuildMesh(
            world, it->entities[i], &terrain[i], config->water_level,
            frozen, state, render->shader);
    }
}

static void WaterUpdate(ecs_iter_t *it) {
    ecs_world_t *world = it->world;
    const Weather *weather = ecs_field(it, Weather, 0);
    WaterMeshState *state = ecs_field(it, WaterMeshState, 1);
    bool visible = biomeWeatherWaterIsCondensed(
        weather->temperature, weather->pressure);
    float frozen = biomeWaterFrozen(weather->temperature);

    for (int32_t i = 0; i < it->count; i ++) {
        biomeWaterUpdateFrozen(world, &state[i], frozen);
        biomeWaterSetVisible(world, &state[i], visible);
    }
}

void biomeWaterConfigureRenderer(
    ecs_world_t *world)
{
    const WaterRenderState *render = ecs_singleton_get(
        world, WaterRenderState);
    if (!render || !render->shader) {
        return;
    }
    ecs_iter_t it = ecs_each(world, FlecsRenderView);
    while (ecs_each_next(&it)) {
        for (int32_t i = 0; i < it.count; i ++) {
            ecs_entity_t geometry = ecs_lookup_child(
                world, it.entities[i], "geometry");
            if (!geometry) {
                continue;
            }
            ecs_entity_t batch = ecs_lookup_child(
                world, geometry, "WaterShader");
            if (!batch) {
                batch = flecsEngine_createMeshShaderBatch(
                    world, geometry, "WaterShader",
                    render->shader, true);
            }
            flecsEngine_renderBatchSetDepthWrite(world, batch, true);
            ecs_entity_t transparent = ecs_lookup_child(
                world, geometry, "TransparentMeshes");
            flecsEngine_renderBatchSetInsertBefore(
                world, geometry, batch, transparent);
        }
    }
}

void biomeWaterImport(ecs_world_t *world) {
    ECS_MODULE(world, biomeWater);

    ECS_IMPORT(world, biomeWeather);

    ecs_set_name_prefix(world, "Water");
    ECS_COMPONENT_DEFINE(world, WaterMeshState);
    ECS_COMPONENT_DEFINE(world, WaterRenderState);

    ecs_set_hooks(world, WaterMeshState, {
        .ctor = flecs_default_ctor
    });

    ecs_add_id(world, ecs_id(WaterRenderState), EcsSingleton);
    ecs_add_pair(
        world, ecs_id(Terrain), EcsWith, ecs_id(WaterMeshState));

    ecs_entity_t shader = flecsEngine_createShader(
        world, ecs_id(biomeWater), "WaterShader",
        &(FlecsShader){
            .source = biomeWaterShaderSource,
            .vertex_entry = "vs_main",
            .fragment_entry = "fs_main"
        });
    ecs_singleton_set(world, WaterRenderState, {
        .shader = shader
    });

    ecs_observer(world, {
        .query.terms = {{ ecs_id(FlecsTerrain) }},
        .events = { EcsOnSet },
        .callback = WaterUpdateMesh
    });

    ECS_SYSTEM(world, WaterUpdate, EcsPreStore,
        [in] Weather,
        [inout] WaterMeshState);

    ecs_set_interval(world, WaterUpdate, 0.1);
}
