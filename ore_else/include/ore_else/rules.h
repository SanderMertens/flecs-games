#ifndef ORE_ELSE_RULES_H
#define ORE_ELSE_RULES_H

#include <flecs.h>
#include <flecs_engine.h>

#undef ECS_META_IMPL
#ifndef ORE_ELSE_RULES_IMPL
#define ECS_META_IMPL EXTERN
#endif

ECS_STRUCT(OreBuildingRule2x1, {
    ecs_vec(ecs_entity_t) left;
    ecs_vec(ecs_entity_t) right;
    ecs_vec(ecs_entity_t) top;
    ecs_vec(ecs_entity_t) bottom;
    ecs_entity_t out;
    bool rotate;
});

void oreRulesEvalCell(
    ecs_world_t *world,
    OreGame *game,
    int32_t row,
    int32_t col);

void oreRulesClear(
    ecs_world_t *world,
    OreGame *game);

void oreRulesImport(ecs_world_t *world);

#endif
