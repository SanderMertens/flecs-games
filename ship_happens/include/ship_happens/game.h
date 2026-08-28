#ifndef SHIP_HAPPENS_GAME_H
#define SHIP_HAPPENS_GAME_H

#include <flecs.h>
#include <flecs_engine.h>

#undef ECS_META_IMPL
#ifndef SHIP_HAPPENS_GAME_IMPL
#define ECS_META_IMPL EXTERN
#endif

#define SHIP_ROWS (7)
#define SHIP_COLS (11)

#define SHIP_DEV_ARRIVE (0)
#define SHIP_DEV_WORK (1)
#define SHIP_DEV_SEEK (2)
#define SHIP_DEV_USE (3)
#define SHIP_DEV_QUIT (4)

#define SHIP_NEED_CAFFEINE (0)
#define SHIP_NEED_FOOD (1)
#define SHIP_NEED_FUN (2)

ECS_ENUM(ShipGameState, {
    ShipGameStatePlaying,
    ShipGameStateShipped,
    ShipGameStateMissed
});

ECS_STRUCT(ShipOffice, {
    float x0;
    float z0;
    float tile;
});

ECS_STRUCT(ShipGame, {
    int32_t money;
    int32_t features_total;
    int32_t features_done;
    float feature_progress;
    float deadline_left;
    float work_rate;
    float behind_frac;
    char *game_name;
    char *feature_name;
    char *clock;
    int32_t devs;
    int32_t applicants;
    ecs_entity_t selected;
    bool bulldoze;
    bool crunch;
    float avg_mood;
    bool morale_bonus;
    float apply_timer;
    ecs_entity_t camera;
    ecs_entity_t furniture;
    ecs_entity_t staff;
    ecs_entity_t dollars;
    ecs_entity_t dollar_prefab;
    ecs_entity_t marker_prefab;
    ecs_entity_t fx_pool;
    ecs_entity_t glow_pool;
    ecs_entity_t poof_burst;
    ecs_entity_t ship_burst;

ECS_PRIVATE
    ecs_query_t *desk_query;
    ecs_query_t *amenity_query;
    ecs_query_t *applicant_query;
    ecs_entity_t grid[SHIP_ROWS * SHIP_COLS];
    int32_t hover_row;
    int32_t hover_col;
    int32_t deadline_secs;
    float mood_sum;
    int32_t mood_count;
});

ECS_STRUCT(ShipCost, {
    int32_t money;
});

ECS_STRUCT(ShipPacket, {
    ecs_entity_t item;
});

ECS_STRUCT(ShipFurniture, {
    int32_t row;
    int32_t col;
});

ECS_STRUCT(ShipDesk, {
    ecs_entity_t owner;
});

ECS_STRUCT(ShipAmenity, {
    int32_t need;
    float use_time;
    ecs_entity_t user;
});

ECS_STRUCT(ShipAura, {
    float radius;
    float mood;
});

ECS_STRUCT(Dev, {
    int32_t outfit;
    int32_t skin;
    int32_t hair;
    int32_t phones;
    float mood;
    float energy;
});

ECS_STRUCT(ShipApplicant, {
    int32_t slot;
    bool moving;
    float tx;
    float tz;
});

ECS_STRUCT(ShipDev, {
    int32_t state;
    bool moving;
    float timer;
    float pay_timer;
    float mood_timer;
    float tx;
    float tz;
    ecs_entity_t desk;
    ecs_entity_t station;
});

ECS_STRUCT(ShipDevNeeds, {
    float caffeine;
    float food;
    float fun;
});

ECS_STRUCT(ShipFloater, {
    float age;
    float life;
    float rise;
});

extern ECS_TAG_DECLARE(ShipDollarPart);

ECS_STRUCT(ShipLimb, {
    float swing;
    float arm;
});

ECS_STRUCT(ShipAnim, {
    float value;
});

ECS_STRUCT(ShipAnimSpeed, {
    float value;
});

void Ship_happensImport(ecs_world_t *world);

#endif
