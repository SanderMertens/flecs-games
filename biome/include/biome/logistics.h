#ifndef FLECS_LOGISTICS_H
#define FLECS_LOGISTICS_H

#undef ECS_META_IMPL
#ifndef BIOME_LOGISTICS_IMPL
#define ECS_META_IMPL EXTERN
#endif

ECS_ENUM(biome_logisticsRequestKind_t, {
    BiomeRequestPickup,
    BiomeRequestDropOff
});

// A request created by a building.
ECS_STRUCT(BiomeLogisticsRequest, {
    biome_logisticsRequestKind_t kind;
    ecs_entity_t source;
    ecs_entity_t resource;
    int32_t amount;
    int32_t priority;
});

ECS_STRUCT(BiomeLogisticsCarrier, {
    ecs_entity_t home;
    ecs_entity_t storage;
});

ECS_STRUCT(BiomeLogisticsJob, {
    ecs_entity_t resource;
    int32_t amount;
    ecs_entity_t src;
    ecs_entity_t dst;
});

void biome_logistics_postRequest(
    ecs_world_t *world,
    biome_logisticsRequestKind_t kind,
    ecs_entity_t source,
    ecs_entity_t resource,
    int32_t amount,
    int32_t priority);

void biomeLogisticsImport(ecs_world_t *world);

#endif
