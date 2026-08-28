#ifndef BIOME_WATER_H
#define BIOME_WATER_H

#undef ECS_META_IMPL
#ifndef BIOME_WATER_IMPL
#define ECS_META_IMPL EXTERN
#endif

void biomeWaterImport(ecs_world_t *world);

void biomeWaterConfigureRenderer(
    ecs_world_t *world);

#endif
