#include "biome.h"

static float biome_placement_maxHeight(
    const FlecsTerrain *terrain,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height)
{
    float result = flecsEngine_terrainCellHeight(terrain, x, y);
    for (int32_t cy = y; cy < y + height; cy ++) {
        for (int32_t cx = x; cx < x + width; cx ++) {
            float cell_height = flecsEngine_terrainCellHeight(
                terrain, cx, cy);
            if (cell_height > result) {
                result = cell_height;
            }
        }
    }
    return result;
}

static TerrainOccupancy* biome_placement_occupancy(
    ecs_world_t *world,
    ecs_entity_t terrain)
{
    const FlecsTerrain *t = ecs_get(world, terrain, FlecsTerrain);
    if (!t || ecs_vec_count(&t->layerTypes) <= TerrainOccupancyIndex) {
        return NULL;
    }
    return flecsEngine_terrain_getLayer(
        world, terrain, TerrainOccupancyIndex, TerrainOccupancy);
}

static uint64_t biome_placement_buildingMask(
    ecs_world_t *world,
    ecs_entity_t prefab)
{
    const BiomeBuildingBit *bit = ecs_get(
        world, prefab, BiomeBuildingBit);
    if (!bit || *bit < 0 || *bit >= 64) {
        return 0;
    }
    return 1llu << *bit;
}

static const EcsScriptFunction* biome_placement_spawnFunction(
    ecs_world_t *world,
    ecs_entity_t building,
    ecs_entity_t function)
{
    const EcsScriptFunction *f = ecs_get(
        world, function, EcsScriptFunction);
    const ecs_script_parameter_t *params = f
        ? ecs_vec_first_t(&f->params, ecs_script_parameter_t)
        : NULL;
    if (!f || !f->callback || f->return_type != ecs_id(ecs_entity_t) ||
        ecs_vec_count(&f->params) != 1 ||
        params[0].type != ecs_id(ecs_entity_t))
    {
        ecs_warn("building '%s' has invalid spawn function '%s'",
            ecs_get_name(world, building), ecs_get_name(world, function));
        return NULL;
    }
    return f;
}

static ecs_entity_t biome_placement_spawnWithFunction(
    ecs_world_t *world,
    ecs_entity_t function,
    ecs_entity_t building)
{
    ecs_entity_t result = 0;
    ecs_value_t arg = {
        .type = ecs_id(ecs_entity_t),
        .ptr = &building
    };
    ecs_value_t value = {
        .type = ecs_id(ecs_entity_t),
        .ptr = &result
    };
    if (ecs_function_call(world, function, 1, &arg, &value)) {
        return 0;
    }
    return result;
}

static void biome_placement_spawn(
    ecs_world_t *world,
    ecs_entity_t building,
    ecs_entity_t parent,
    ecs_entity_t terrain,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    const BiomeBuilding *config)
{
    BiomeBuildingSpawn spawn = config->spawn;
    if (!spawn.prefab || !ecs_is_alive(world, spawn.prefab)) {
        return;
    }

    const EcsScriptFunction *function = ecs_get(
        world, spawn.prefab, EcsScriptFunction);
    if (function) {
        function = biome_placement_spawnFunction(
            world, building, spawn.prefab);
        if (!function) {
            return;
        }
    }

    FlecsPosition3 position;
    if (!flecsEngine_terrainTileToPosition(
        world, terrain, x, y, width, height,
        &spawn.position, &position))
    {
        return;
    }
    if (!function && parent == terrain) {
        const FlecsPosition3 *terrain_position = ecs_get(
            world, terrain, FlecsPosition3);
        if (terrain_position) {
            position.x -= terrain_position->x;
            position.y -= terrain_position->y;
            position.z -= terrain_position->z;
        }
    }

    ecs_entity_t instance;
    if (function) {
        instance = biome_placement_spawnWithFunction(
            world, spawn.prefab, building);
    } else {
        instance = ecs_new_w_pair(world, EcsIsA, spawn.prefab);
        if (parent) {
            ecs_add_pair(world, instance, EcsChildOf, parent);
        }
    }
    if (!instance || !ecs_is_alive(world, instance)) {
        ecs_warn("building '%s' spawn function returned an invalid entity",
            ecs_get_name(world, building));
        return;
    }

    ecs_set_ptr(world, instance, FlecsPosition3, &position);
    ecs_set(world, instance, FlecsRotation3, {
        spawn.rotation.x, spawn.rotation.y, spawn.rotation.z
    });
}

ecs_entity_t biomePlaceBuilding(
    ecs_world_t *world,
    ecs_entity_t prefab,
    ecs_entity_t terrain,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    ecs_entity_t effect)
{
    const FlecsTerrain *t = ecs_get(world, terrain, FlecsTerrain);
    const BiomeBuilding *config = ecs_get(world, prefab, BiomeBuilding);
    if (!t || width < 1 || height < 1 ||
        x < 0 || y < 0 || x + width > t->width || y + height > t->depth)
    {
        return 0;
    }

    if (width > 1 || height > 1) {
        float target_height = biome_placement_maxHeight(
            t, x, y, width, height);
        flecsEngine_terrain_setHeight(
            world, terrain, x, y, width, height, target_height);
    }

    ecs_entity_t parent = ecs_lookup(world, "scene.buildings");
    if (!parent) {
        parent = terrain;
    }

    ecs_entity_t building = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_add_pair(world, building, EcsChildOf, parent);
    ecs_set(world, building, FlecsTerrainPosition, {
        .terrain = terrain, .x = x, .y = y,
        .span_x = width, .span_y = height });

    if (config) {
        biome_placement_spawn(
            world, building, parent, terrain, x, y, width, height, config);
    }

    const FlecsParticleBurst *place_burst = effect
        ? ecs_get(world, effect, FlecsParticleBurst)
        : NULL;
    if (place_burst) {
        FlecsParticleBurst burst = *place_burst;
        burst.count = place_burst->count * width * height;
        burst.spread.x = place_burst->spread.x +
            (float)(width - 1) * t->cell_size;
        burst.spread.z = place_burst->spread.z +
            (float)(height - 1) * t->cell_size;
        ecs_set_ptr(world, building, FlecsParticleBurst, &burst);
    }

    uint64_t mask = biome_placement_buildingMask(world, prefab);
    TerrainOccupancy *occupancy = biome_placement_occupancy(world, terrain);
    if (occupancy) {
        for (int32_t cy = y; cy < y + height; cy ++) {
            for (int32_t cx = x; cx < x + width; cx ++) {
                occupancy[cy * t->width + cx].buildings |= mask;
            }
        }
    }

    biome_terrainItemIndex_place(world, building);
    biome_playerAttr_addFlag(world, "BuildingsPlaced", mask);
    return building;
}
