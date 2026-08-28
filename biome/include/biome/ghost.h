#ifndef BIOME_GHOST_H
#define BIOME_GHOST_H

/* Ghosts are cached prefab variants of regular prefabs with a ghost tint
 * applied. Because they are prefabs they are invisible to game queries, and
 * are rendered with the engine's immediate mode drawing API. */

ecs_entity_t biomeGhostGet(
    ecs_world_t *world,
    ecs_entity_t prefab);

void biomeGhostImport(ecs_world_t *world);

#endif
