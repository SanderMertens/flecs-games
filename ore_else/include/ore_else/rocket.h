#ifndef ORE_ELSE_ROCKET_H
#define ORE_ELSE_ROCKET_H

#include <flecs.h>
#include <flecs_engine.h>

#undef ECS_META_IMPL
#ifndef ORE_ELSE_ROCKET_IMPL
#define ECS_META_IMPL EXTERN
#endif

ECS_ENUM(OreLaunchPhase, {
    OreLaunchPhaseIdle,
    OreLaunchPhaseIgnition,
    OreLaunchPhaseAscent
});

ECS_STRUCT(OreRocketKit, {
    ecs_entity_t root;
    ecs_entity_t engine_item;
    ecs_entity_t fuel_item;
    ecs_entity_t cargo_item;
    ecs_entity_t life_item;
    ecs_entity_t luminite;
    ecs_entity_t model;
    ecs_entity_t flame;
    ecs_entity_t smoke;
});

ECS_STRUCT(OreRocketState, {
    OreRocketKit kit;
    ecs_entity_t pad;
    ecs_entity_t rocket;
    int32_t engines;
    int32_t fuel;
    int32_t cargo_bays;
    int32_t life_support;
    int32_t luminite;
    OreLaunchPhase phase;
    float timer;
    float speed;
    float fx_timer;
    char *status_text;
    char *note;
    bool valid;
    bool boarded;

ECS_PRIVATE
    ecs_query_t *pad_query;
});

ECS_STRUCT(OreLaunchPad, {
    float base_y;
});

ECS_STRUCT(OreRocketPacket, {
    ecs_entity_t item;
    bool load;
    bool launch;
    bool remove;
});

void oreRocketReset(
    ecs_world_t *world,
    OreRocketState *rocket);

void oreRocketClick(
    ecs_world_t *world,
    ecs_entity_t widget);

void oreRocketImport(ecs_world_t *world);

#endif
