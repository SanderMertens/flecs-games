#define BIOME_GHOST_IMPL

#include "biome.h"

ecs_entity_t biomeGhostGet(
    ecs_world_t *world,
    ecs_entity_t prefab)
{
    if (!prefab || !ecs_is_alive(world, prefab)) {
        return 0;
    }

    ecs_entity_t scope = ecs_lookup(world, "ghost");
    if (!scope) {
        return 0;
    }

    char id_name[32];
    const char *name = ecs_get_name(world, prefab);
    if (!name) {
        ecs_os_snprintf(id_name, sizeof(id_name), "e%u", (uint32_t)prefab);
        name = id_name;
    }

    ecs_entity_t ghost = ecs_lookup_child(world, scope, name);
    if (ghost) {
        return ghost;
    }

    ghost = ecs_entity(world, { .name = name, .parent = scope });
    ecs_add_id(world, ghost, EcsPrefab);
    ecs_add_pair(world, ghost, EcsIsA, prefab);
    ecs_set(world, ghost, FlecsRgba, { 90, 150, 255, 120 });
    ecs_add_id(world, ghost, FlecsAlphaBlend);

    return ghost;
}

void biomeGhostImport(ecs_world_t *world) {
    ECS_MODULE(world, biomeGhost);

    ecs_entity_t old_scope = ecs_set_scope(world, 0);
    ecs_entity(world, { .name = "ghost" });
    ecs_set_scope(world, old_scope);
}
