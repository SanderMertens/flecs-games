#ifndef ORE_ELSE_DRONES_H
#define ORE_ELSE_DRONES_H

#include <flecs.h>
#include <flecs_engine.h>

#undef ECS_META_IMPL
#ifndef ORE_ELSE_DRONES_IMPL
#define ECS_META_IMPL EXTERN
#endif

#define ORE_DRONE_ROTORS (4)

ECS_ENUM(OreDroneState, {
    OreDroneStateDocked,
    OreDroneStateFlying,
    OreDroneStateRepairing,
    OreDroneStateReturning
});

ECS_STRUCT(OreDroneBay, {
    int32_t count;
    ecs_entity_t drone;
    float radius;
    float dock;
    float scan;
});

ECS_STRUCT(OreRepairRate, {
    float value;
    float per_iron;
});

ECS_STRUCT(OreFlight, {
    float cruise;
    float arrive;
    float ease;
    float bob;
    float bob_rate;
    float turn;
    float rotor;
    float spin_up;
});

ECS_STRUCT(OreDrone, {
    ecs_entity_t pad;
    ecs_entity_t target;
    ecs_entity_t fx;
    ecs_entity_t rotors[4];
    OreDroneState state;
    int32_t bay;
    float phase;
    float scan;
    float credit;
    float spin;
});

void oreDronesImport(ecs_world_t *world);

#endif
