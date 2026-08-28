#ifndef FLECS_RESOURCES_H
#define FLECS_RESOURCES_H

#undef ECS_META_IMPL
#ifndef BIOME_RESOURCES_IMPL
#define ECS_META_IMPL EXTERN
#endif

extern ECS_COMPONENT_DECLARE(biome_resource_storageKind_t);
extern ECS_COMPONENT_DECLARE(BiomeResourceStorageMap);

/* Storage kind */
typedef enum {
    BiomeResourceStorageKindSource,
    BiomeResourceStorageKindSink,
    BiomeResourceStorageKindFactory,
} biome_resource_storageKind_t;

/* Resource parameters */
ECS_STRUCT(BiomeResource, {
    int32_t mine_time;          /* Time it takes for an extractor to mine a resource in frames. */
    int32_t mine_amount;        /* Number of resources mined after mine_time (or however many are left). */
    int32_t min_drone_amount;  /* Minimum number of resources for a pickup (drones won't consider storages with less than this) */
    int32_t max_drone_amount;  /* Maximum number of resources a drone can carry for resource. Drones will combine outstanding requests up to this amount. */
    float greenhouse_gas;
    float toxic_gass;
    float o2;
    float fertility;
    float moisture;
});

/* Resource storage specification. */
ECS_STRUCT(BiomeResourceStorageDesc, {
    biome_resource_storageKind_t kind;
    int32_t capacity;
});

typedef ecs_map_t BiomeResourceStorageMap;

ECS_STRUCT(BiomePlayerStorage, {
    int32_t capacity;
    BiomeResourceStorageMap resources;
});

/* Live storage state */
ECS_STRUCT(BiomeResourceStorage, {
    BiomeResourceStorageMap resources;  /* Resources available in storage */
    BiomeResourceStorageMap reserved;   /* Resources reserved for pickup */
    ecs_vec(ecs_entity_t) outstanding_requests;    /* Vector with outstanding unaccepted requests. */
});

/* Component that describes how to create a resource */
ECS_STRUCT(BiomeRecipe, {
    BiomeResourceStorageMap inputs;
    ecs_entity_t output;
    BiomeResourceStorageMap outputs;
    float craft_time;
});

void biomeResourcesImport(ecs_world_t *world);

int32_t biome_resource_playerAmount(
    ecs_world_t *world,
    const char *attribute,
    ecs_entity_t resource);

bool biome_resource_playerAdd(
    ecs_world_t *world,
    const char *attribute,
    ecs_entity_t resource,
    int32_t amount);

#endif
