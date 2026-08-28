#ifndef ORE_ELSE_RESOURCES_H
#define ORE_ELSE_RESOURCES_H

#include <flecs.h>
#include <flecs_engine.h>

#undef ECS_META_IMPL
#ifndef ORE_ELSE_RESOURCES_IMPL
#define ECS_META_IMPL EXTERN
#endif

extern ECS_COMPONENT_DECLARE(OreStorageMap);

typedef ecs_map_t OreStorageMap;

ECS_STRUCT(OreResource, {
    int32_t mine_time_ds;
    int32_t mine_amount;
});

ECS_STRUCT(OreRecipe, {
    OreStorageMap inputs;
    ecs_entity_t output;
    int32_t output_amount;
    float craft_time;
});

ECS_STRUCT(OreIcon, {
    ecs_entity_t texture;
});

ECS_STRUCT(OreDeposit, {
    ecs_entity_t resource;
    int32_t amount;
    int32_t capacity;
    int32_t row;
    int32_t col;
});

void oreResourcesImport(ecs_world_t *world);

ecs_value_t orePlayerAttrGet(
    ecs_world_t *world,
    const char *name);

int32_t oreInventoryGet(
    ecs_world_t *world,
    ecs_entity_t resource);

int32_t oreInventoryKinds(
    ecs_world_t *world);

bool oreInventoryFits(
    ecs_world_t *world,
    const ecs_entity_t *items,
    const int32_t *amounts,
    int32_t count);

bool oreInventoryAdd(
    ecs_world_t *world,
    ecs_entity_t resource,
    int32_t amount);

#endif
