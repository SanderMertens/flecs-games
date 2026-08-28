#ifndef BIOME_PLACEMENT_H
#define BIOME_PLACEMENT_H

ecs_entity_t biomePlaceBuilding(
    ecs_world_t *world,
    ecs_entity_t prefab,
    ecs_entity_t terrain,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    ecs_entity_t effect);

#endif
