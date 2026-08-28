#ifndef FLECS_BUILDINGS_H
#define FLECS_BUILDINGS_H

#undef ECS_META_IMPL
#ifndef BIOME_BUILDINGS_IMPL
#define ECS_META_IMPL EXTERN
#endif

/* Resource parameters */
ECS_STRUCT(BiomeBuildingSpawn, {
    ecs_entity_t prefab;
    flecs_vec3_t position;
    flecs_vec3_t rotation;
});

ECS_STRUCT(BiomeBuilding, {
    int32_t max_count;       /* Max number of times this building can be built */
    bool can_doze;           /* Can building be dozed */
    flecs_vec2_t footprint;  /* Footprint of building (measured in tiles) */
    float drag_stride;       /* Space between instances when dragging (measured in tiles)*/
    ecs_vec(ecs_entity_t) requires; /* Can only be placed on tile with specified building(s) */
    BiomeBuildingSpawn spawn;
    ecs_entity_t rule;       /* Optional rule that must match before placement */
});

typedef int8_t BiomeBuildingBit;
extern ECS_COMPONENT_DECLARE(BiomeBuildingBit);

typedef uint64_t BiomeBuildingRequirementMask;
extern ECS_COMPONENT_DECLARE(BiomeBuildingRequirementMask);

/* Emitted after the occupancy grid has been updated for a building. */
ECS_STRUCT(BiomeBuildingOccupancyChanged, {
    ecs_entity_t terrain;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    int8_t building_bit;
    bool occupied;
});

void biomeBuildingsImport(ecs_world_t *world);

#endif
