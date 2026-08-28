#ifndef LETTUCE_PRAY_GAME_H
#define LETTUCE_PRAY_GAME_H

#include <flecs.h>
#include <flecs_engine.h>

#undef ECS_META_IMPL
#ifndef LETTUCE_PRAY_GAME_IMPL
#define ECS_META_IMPL EXTERN
#endif

#define LETTUCE_ROWS (5)
#define LETTUCE_COLS (9)

ECS_ENUM(LettuceGameState, {
    LettuceGameStatePlaying,
    LettuceGameStateWon,
    LettuceGameStateLost
});

ECS_STRUCT(LettuceLawn, {
    float x0;
    float z0;
    float tile;
});

ECS_STRUCT(LettuceGame, {
    int32_t sun;
    ecs_entity_t selected;
    bool shovel;
    int32_t wave;
    int32_t waves;
    int32_t planted;
    int32_t killed;
    int32_t spawn_left;
    int32_t alive;
    float wave_timer;
    float spawn_timer;
    float sun_timer;
    ecs_entity_t camera;
    ecs_entity_t plants;
    ecs_entity_t zombies;
    ecs_entity_t peas;
    ecs_entity_t suns;
    ecs_entity_t sun_prefab;
    ecs_entity_t pea_prefab;
    ecs_entity_t marker_prefab;
    ecs_entity_t zombie_prefab;
    ecs_entity_t cone_prefab;
    ecs_entity_t bucket_prefab;
    ecs_entity_t fx_pool;
    ecs_entity_t glow_pool;
    ecs_entity_t splat_burst;
    ecs_entity_t chomp_burst;
    ecs_entity_t boom_burst;
    ecs_entity_t pop_burst;
    ecs_entity_t armor_burst;

ECS_PRIVATE
    ecs_query_t *zombie_query;
    ecs_query_t *sun_query;
    ecs_entity_t grid[LETTUCE_ROWS * LETTUCE_COLS];
    float row_max[LETTUCE_ROWS];
    float row_min[LETTUCE_ROWS];
    int32_t hover_row;
    int32_t hover_col;
});

ECS_STRUCT(LettuceCost, {
    int32_t sun;
});

ECS_STRUCT(LettuceSeedPacket, {
    ecs_entity_t plant;
});

ECS_STRUCT(LettuceMaxHealth, {
    float value;
});

ECS_STRUCT(LettuceMaxArmor, {
    float value;
});

ECS_STRUCT(LettuceHealth, {
    float value;
});

ECS_STRUCT(LettuceArmor, {
    float value;
});

ECS_STRUCT(LettuceDamage, {
    float damage;
    float dps;
});

ECS_STRUCT(LettuceSpeed, {
    float value;
});

ECS_STRUCT(LettuceAnimSpeed, {
    float value;
});

ECS_STRUCT(LettuceAnim, {
    float value;
});

ECS_STRUCT(LettuceActionInterval, {
    float value;
});

ECS_STRUCT(LettuceActionTimer, {
    float value;
});

extern ECS_TAG_DECLARE(LettuceProducer);

ECS_STRUCT(LettucePlant, {
    int32_t row;
    int32_t col;
});

ECS_STRUCT(LettuceShooter, {
    float height;
});

ECS_STRUCT(LettuceBomb, {
    float radius;
});

ECS_STRUCT(LettuceZombie, {
    int32_t row;
    ecs_entity_t eating;
});

ECS_STRUCT(LettuceLimb, {
    float amount;
    float bias;
});

ECS_STRUCT(LettuceCorpse, {
    float time;
});

ECS_STRUCT(LettucePea, {
    int32_t row;
});

ECS_STRUCT(LettuceSunDrop, {
    int32_t value;
    float land_y;
    float fall_speed;
});

ECS_STRUCT(LettuceSunState, {
    float life;
    float collect_time;
});

ECS_STRUCT(LettuceMower, {
    int32_t row;
    bool running;
});

void Lettuce_prayImport(ecs_world_t *world);

#endif
