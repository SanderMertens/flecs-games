#ifndef BIOME_TOOL_H
#define BIOME_TOOL_H

#undef ECS_META_IMPL
#ifndef BIOME_TOOL_IMPL
#define ECS_META_IMPL EXTERN
#endif

ECS_STRUCT(BiomeTool, {
    ecs_entity_t building;
ECS_PRIVATE
    ecs_entity_t place_effect;
    ecs_entity_t doze_effect;
    bool doze;
    bool dragging;
    int32_t anchor_x;
    int32_t anchor_y;
});

void biomeToolPlaceBuilding(
    ecs_world_t *world,
    ecs_entity_t building);

void biomeToolDoze(
    ecs_world_t *world);

void biomeToolImport(ecs_world_t *world);

#endif
