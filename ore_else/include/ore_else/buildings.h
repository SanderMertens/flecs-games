#ifndef ORE_ELSE_BUILDINGS_H
#define ORE_ELSE_BUILDINGS_H

#include <flecs.h>
#include <flecs_engine.h>

#undef ECS_META_IMPL
#ifndef ORE_ELSE_BUILDINGS_IMPL
#define ECS_META_IMPL EXTERN
#endif

ECS_STRUCT(OreBuilding, {
    int32_t row;
    int32_t col;
});

ECS_STRUCT(OreAge, {
    float value;
});

ECS_STRUCT(OreWear, {
    float dust;
    float burn;
});

ECS_STRUCT(OreDrillState, {
    float timer;
    bool running;
});

ECS_STRUCT(OreSpinner, {
    float speed;
    ecs_entity_t owner;
});

ECS_STRUCT(OrePiston, {
    float rest;
    float travel;
    float speed;
    float phase;
    float impact;
    ecs_entity_t burst;
    ecs_entity_t spark;
    ecs_entity_t owner;
    bool slide;
});

ECS_STRUCT(OreShake, {
    float amplitude;
    float frequency;
    float phase;
    float rest;
    ecs_entity_t owner;
    bool ready;
});

ECS_STRUCT(OreWorkLight, {
    float on;
    float off;
    float blink;
    float light;
    float pulse;
    float hold;
    float hot;
    float level;
    ecs_entity_t owner;
    ecs_entity_t piston;
    bool power;
});

ECS_STRUCT(OreStatusLamp, {
    FlecsRgba live;
    FlecsRgba dead;
    FlecsRgba warn;
    FlecsRgba ok;
    float strength;
    float blink;
    float light;
    ecs_entity_t owner;
    bool consumer;
});

ECS_STRUCT(OreVent, {
    float interval;
    float timer;
    ecs_entity_t burst;
    ecs_entity_t owner;
});

ECS_STRUCT(OreWorkEmitter, {
    ecs_entity_t owner;
});

ECS_STRUCT(OreFootprint, {
    int32_t w;
    int32_t h;
});

ECS_STRUCT(OreBuildLimit, {
    int32_t max;
});

ecs_entity_t* oreBuildingCell(
    OreGame *game,
    int32_t row,
    int32_t col);

void oreFootprint(
    ecs_world_t *world,
    ecs_entity_t prefab,
    int32_t *w,
    int32_t *h);

void oreBuildingsImport(ecs_world_t *world);

#endif
