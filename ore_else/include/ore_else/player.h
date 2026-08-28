#ifndef ORE_ELSE_PLAYER_H
#define ORE_ELSE_PLAYER_H

#include <flecs.h>
#include <flecs_engine.h>

#undef ECS_META_IMPL
#ifndef ORE_ELSE_PLAYER_IMPL
#define ECS_META_IMPL EXTERN
#endif

ECS_STRUCT(OreZoom, {
    float height[3];
    float back[3];
    float look[3];
    float glide;
});

ECS_STRUCT(OreViewState, {
    int32_t level;
    float height;
    float back;
    float look;
});

ECS_STRUCT(OrePlayer, {
    float anim;
    float mine_left;
    float hurt;
    float hover;
    float step;
    ecs_entity_t mine_target;
    ecs_entity_t dust;
    ecs_entity_t jet;
    bool moving;
    bool running;
    bool airborne;
});

ECS_STRUCT(OreMineProgress, {
    float value;
});

ECS_STRUCT(OrePlayerIntent, {
    bool forward;
    bool back;
    bool left;
    bool right;
    bool run;
    bool mine;
});

ECS_STRUCT(OreLimb, {
    float phase;
    float swing;
});

void orePlayerImport(ecs_world_t *world);

#endif
