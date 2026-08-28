#ifndef ORE_ELSE_PLACEMENT_H
#define ORE_ELSE_PLACEMENT_H

#include <flecs.h>
#include <flecs_engine.h>

#undef ECS_META_IMPL
#ifndef ORE_ELSE_PLACEMENT_IMPL
#define ECS_META_IMPL EXTERN
#endif

bool orePaused(ecs_world_t *world);

bool oreRunHeld(ecs_world_t *world, const OreGame *game);

bool oreRejectDrag(ecs_world_t *world);

bool oreCellFree(
    ecs_world_t *world,
    OreGame *game,
    ecs_entity_t prefab,
    int32_t row,
    int32_t col);

ecs_entity_t orePlaceBuilding(
    ecs_world_t *world,
    OreGame *game,
    const OreMap *map,
    ecs_entity_t prefab,
    int32_t row,
    int32_t col);

bool oreDemolish(
    ecs_world_t *world,
    OreGame *game,
    const OreMap *map,
    int32_t row,
    int32_t col);

void orePlacementImport(ecs_world_t *world);

#endif
