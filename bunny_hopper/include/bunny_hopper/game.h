#ifndef BUNNY_HOPPER_GAME_H
#define BUNNY_HOPPER_GAME_H

#include <flecs.h>
#include <flecs_engine.h>

#undef ECS_META_IMPL
#ifndef BUNNY_HOPPER_GAME_IMPL
#define ECS_META_IMPL EXTERN
#endif

ECS_ENUM(BunnyGameState, {
    BunnyGameStatePlaying,
    BunnyGameStateOver
});

ECS_STRUCT(BunnyGame, {
    int32_t carrots;
    int32_t max_lives;
    float distance_since_spawn;
    float next_spawn_distance;
    ecs_entity_t player;
    ecs_entity_t dust_pool;
    ecs_entity_t dust_burst_low;
    ecs_entity_t dust_burst_high;
    ecs_entity_t hit_pool;
    ecs_entity_t hit_burst;
    ecs_entity_t sparkle_pool;
    ecs_entity_t sparkle_burst;
    ecs_entity_t carrot_small_prefab;
    ecs_entity_t carrot_large_prefab;
});

ECS_STRUCT(Bunny, {
    float speed;
    bool dead;
});

ECS_STRUCT(BunnyJump, {
    float v;
    int32_t jumps;
});

ECS_STRUCT(BunnyLives, {
    int32_t lives_left;
});

ECS_STRUCT(BunnyPlayer, {
    float run_phase;
});

ECS_STRUCT(BunnyInvulnerable, {
    float time_left;
});

ECS_STRUCT(BunnyLeg, {
    float phase;
    float air_target;
});

ECS_STRUCT(BunnyCarrot, {
    float half_width;
    float height;
});

ECS_STRUCT(BunnyScenery, {
    float parallax;
    float wrap_min;
    float wrap_span;
});

void Bunny_hopperImport(ecs_world_t *world);

#endif
