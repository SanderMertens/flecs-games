#ifndef ORE_ELSE_QUALITY_H
#define ORE_ELSE_QUALITY_H

#include <flecs.h>
#include <flecs_engine.h>

extern ECS_TAG_DECLARE(OreQualityCycle);

void oreQualityClick(
    ecs_world_t *world,
    ecs_entity_t widget);

void oreQualityImport(ecs_world_t *world);

#endif
