#ifndef BIOME_TERRAIN_ITEM_INDEX_H
#define BIOME_TERRAIN_ITEM_INDEX_H

typedef struct TerrainItemRecord {
    ecs_vec(ecs_entity_t) entities;
} TerrainItemRecord;

typedef struct TerrainItemIndex {
    ecs_map(uint64_t, TerrainItemRecord) records;
} TerrainItemIndex;

extern ECS_COMPONENT_DECLARE(TerrainItemIndex);

uint64_t biome_terrainItemIndex_pos(
    int32_t x,
    int32_t y);

const TerrainItemRecord* biome_terrainItemIndex_get(
    const ecs_world_t *world,
    int32_t x,
    int32_t y);

void biome_terrainItemIndex_place(
    ecs_world_t *world,
    ecs_entity_t entity);

void biome_terrainItemIndex_remove(
    ecs_world_t *world,
    ecs_entity_t entity);

void biomeTerrainItemIndexImport(ecs_world_t *world);

#endif
