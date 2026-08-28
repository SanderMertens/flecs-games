#ifndef BIOME_TASKS_LOGISTICS_H
#define BIOME_TASKS_LOGISTICS_H

#include "biome.h"

typedef struct BiomeLogisticsWaiter {
    ecs_script_future_t *future;
} BiomeLogisticsWaiter;

typedef struct BiomeLogisticsMotion {
    ecs_script_future_t *future;
    FlecsPosition3 start;
    FlecsPosition3 target;
    ecs_entity_t terrain;
    float start_clearance;
    float target_clearance;
    int32_t elapsed;
    int32_t duration;
} BiomeLogisticsMotion;

ECS_COMPONENT_DECLARE(BiomeLogisticsWaiter);
ECS_COMPONENT_DECLARE(BiomeLogisticsMotion);

int32_t biome_logistics_mapValue(
    const BiomeResourceStorageMap *map,
    ecs_entity_t resource);

void biome_logistics_addMapValue(
    BiomeResourceStorageMap *map,
    ecs_entity_t resource,
    int32_t amount);

void biomeLogisticsTasksImport(
    ecs_world_t *world,
    ecs_entity_t module);

#endif
