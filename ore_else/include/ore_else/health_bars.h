#ifndef ORE_ELSE_HEALTH_BARS_H
#define ORE_ELSE_HEALTH_BARS_H

#include <flecs.h>
#include <flecs_engine.h>

#undef ECS_META_IMPL
#ifndef ORE_ELSE_HEALTH_BARS_IMPL
#define ECS_META_IMPL EXTERN
#endif

ECS_STRUCT(OreBounds, {
    float min[3];
    float max[3];
});

void oreHealthBarsImport(
    ecs_world_t *world);

#endif
