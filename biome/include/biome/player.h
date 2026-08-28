#ifndef BIOME_PLAYER_H
#define BIOME_PLAYER_H

void biome_playerAttr_addFlag(
    ecs_world_t *world,
    const char *name,
    int64_t flag);

void biome_playerAttr_set(
    ecs_world_t *world,
    const char *name,
    const ecs_value_t *value);

ecs_value_t biome_playerAttr_get(
    ecs_world_t *world,
    const char *name);

#endif
