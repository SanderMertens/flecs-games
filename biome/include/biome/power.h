#ifndef FLECS_POWER_H
#define FLECS_POWER_H

#undef ECS_META_IMPL
#ifndef BIOME_POWER_IMPL
#define ECS_META_IMPL EXTERN
#endif

/* Static power configuration for placeable items */
ECS_STRUCT(BiomePower, {
    bool producer;     /* Does building produce power */
    bool conductor;    /* Does building conduct power to neighboring tiles */
    float demand;      /* How much power does the building demand */
});

/* Dynamic power configuration power consumers */
ECS_STRUCT(BiomePowerConsumer, {
    bool powered;
    ecs_entity_t network;
});

/* Dynamic power configuration power providers */
ECS_STRUCT(BiomePowerProducer, {
    float production;
});

ECS_STRUCT(TerrainPower, {
    uint32_t generation;
    ecs_entity_t network;
    uint32_t distance;
});

ECS_STRUCT(BiomePowerNetworkConsumer, {
    ecs_entity_t entity;
    uint32_t distance;
    float demand;
});

extern ECS_COMPONENT_DECLARE(BiomePowerNetworkConsumers);
typedef ecs_vec_t BiomePowerNetworkConsumers;

ECS_STRUCT(BiomePowerNetwork, {
    float production;
    int32_t producer_count;
    BiomePowerNetworkConsumers consumers;
});

typedef struct BiomePowerGrid {
    bool dirty;
    uint32_t generation;
    float total_production;
    float total_demand;
    float satisfied_demand;
    ecs_query_t *buildings;
    ecs_query_t *prefabs;
} BiomePowerGrid;

extern ECS_COMPONENT_DECLARE(BiomePowerGrid);

void biome_power_markDirty(ecs_world_t *world);

void biomePowerImport(ecs_world_t *world);

#endif
