#ifndef ORE_ELSE_POWER_H
#define ORE_ELSE_POWER_H

#include <flecs.h>
#include <flecs_engine.h>

#undef ECS_META_IMPL
#ifndef ORE_ELSE_POWER_IMPL
#define ECS_META_IMPL EXTERN
#endif

ECS_STRUCT(OrePowerProducer, {
    float watts;
});

ECS_STRUCT(OrePowerConsumer, {
    float watts;
});

ECS_ENUM(OrePowerKind, {
    OrePowerKindMining,
    OrePowerKindWeapons,
    OrePowerKindConstruction
});

ECS_STRUCT(OrePowerCategory, {
    OrePowerKind kind;
});

ECS_STRUCT(OrePowerAlloc, {
    int32_t level[3];
});

ECS_STRUCT(OrePowerSeg, {
    OrePowerKind kind;
    int32_t level;
});

ECS_STRUCT(OrePowerState, {
    float prod;
    float demand;
    float ratio;
    bool blackout;

ECS_PRIVATE
    ecs_query_t *producer_query;
    ecs_query_t *consumer_query;
});

int32_t orePowerLevel(
    ecs_world_t *world,
    ecs_entity_t e);

float orePowerMul(
    ecs_world_t *world,
    ecs_entity_t e);

void orePowerSegClick(
    ecs_world_t *world,
    ecs_entity_t widget);

void orePowerImport(ecs_world_t *world);

#endif
