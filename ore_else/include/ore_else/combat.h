#ifndef ORE_ELSE_COMBAT_H
#define ORE_ELSE_COMBAT_H

#include <flecs.h>
#include <flecs_engine.h>

#undef ECS_META_IMPL
#ifndef ORE_ELSE_COMBAT_IMPL
#define ECS_META_IMPL EXTERN
#endif

#define ORE_TIERS (4)

ECS_STRUCT(OreHealth, {
    float value;
    float max;
});

ECS_STRUCT(OreSpeed, {
    float value;
});

ECS_STRUCT(OreVelocity, {
    float x;
    float z;
});

ECS_STRUCT(OreDamage, {
    float value;
});

ECS_STRUCT(OreRange, {
    float value;
});

ECS_STRUCT(OreAttackInterval, {
    float value;
});

ECS_STRUCT(OreAttackTimer, {
    float value;
});

ECS_STRUCT(OreSplash, {
    float radius;
});

ECS_STRUCT(OreMuzzle, {
    float speed;
    float height;
    float forward;
});

ECS_STRUCT(OreAmmo, {
    ecs_entity_t projectile;
});

ECS_STRUCT(OreBeam, {
    ecs_entity_t prefab;
});

ECS_STRUCT(OreTarget, {
    ecs_entity_t value;
});

ECS_STRUCT(OreCritter, {
    float retarget;
    float anim;
    float step;
    float bob;
    float jitter;
    bool attacking;
});

ECS_STRUCT(OreRadius, {
    float value;
});

ECS_STRUCT(OreCorpse, {
    float timer;
    float sink;
    float depth;
});

ECS_STRUCT(OreProjectile, {
    float vx;
    float vy;
    float vz;
    float life;
    float radius;
    float gravity;
    bool hits_critters;
});

ECS_STRUCT(OreHoming, {
    float turn_rate;
    float reacquire;
});

ECS_STRUCT(OreTurret, {
    ecs_entity_t head;
    ecs_entity_t fx;
    ecs_entity_t hit_fx;
    float aim;
});

ECS_STRUCT(OreLaunchFx, {
    ecs_entity_t burst;
});

ECS_STRUCT(OreDeathBurst, {
    ecs_entity_t burst;
});

ECS_STRUCT(OreFlashLight, {
    float peak;
    float fade;
    float level;
    bool once;
});

ECS_STRUCT(OreArsenal, {
    ecs_entity_t mite;
    ecs_entity_t skitter;
    ecs_entity_t brute;
    ecs_entity_t behemoth;
    ecs_entity_t spitter;
    ecs_entity_t embers;
    ecs_entity_t splat_burst;
    ecs_entity_t boom_burst;
    ecs_entity_t spark_burst;
    ecs_entity_t impact_dust;
    ecs_entity_t hit_puff;
});

ECS_STRUCT(OreWaveMix, {
    float tier[4];
});

ECS_STRUCT(OreWaveGate, {
    int32_t tier[4];
});

ECS_ENUM(OreWaveStyle, {
    OreWaveStyleMixed,
    OreWaveStyleSwarm,
    OreWaveStyleElite
});

ECS_STRUCT(OreWaveState, {
    int32_t wave;
    int32_t alive;
    int32_t spawn_left;
    int32_t spawn_small;
    int32_t spawn_medium;
    int32_t spawn_big;
    int32_t spawn_huge;
    int32_t kills;
    float budget;
    OreWaveStyle style;
    float timer;
    float spawn_timer;
    float stagger;
    float frac;

ECS_PRIVATE
    float wave_span;
});

ECS_STRUCT(OreCombatState, {
ECS_PRIVATE
    ecs_query_t *critter_query;
    ecs_query_t *crowd_query;
    ecs_query_t *corpse_query;
    ecs_query_t *target_query;
    void *crowd;
    void *index;
});

void oreFxToggle(ecs_world_t *world, ecs_entity_t fx, bool on);

void oreCombatImport(ecs_world_t *world);

#endif
