#include "biome.h"

void biome_playerAttr_addFlag(
    ecs_world_t *world,
    const char *name,
    int64_t flag)
{
    ecs_entity_t player = ecs_lookup(world, "player");
    if (!player) {
        ecs_err("player scope not found");
        return;
    }

    ecs_entity_t var = ecs_lookup_child(world, player, name);
    if (!var) {
        ecs_err("player attribute '%s' not found", name);
        return;
    }

    ecs_value_t value = ecs_const_var_get(world, var);
    if (!value.ptr) {
        ecs_err("player attribute '%s' has no value", name);
        return;
    }

    int64_t *flags = value.ptr;
    int64_t next = *flags | flag;
    if (next == *flags) {
        return;
    }

    *flags = next;

    ecs_const_var_modified(world, var);
}

void biome_playerAttr_set(
    ecs_world_t *world,
    const char *name,
    const ecs_value_t *value)
{
    ecs_entity_t player = ecs_lookup(world, "player");
    if (!player) {
        ecs_err("player scope not found");
        return;
    }

    ecs_entity_t var = ecs_lookup_child(world, player, name);
    if (!var) {
        ecs_err("player attribute '%s' not found", name);
        return;
    }

    ecs_value_t dst = ecs_const_var_get(world, var);
    if (!dst.ptr) {
        ecs_err("player attribute '%s' has no value", name);
        return;
    }

    if (ecs_value_equals(world, &dst, value)) {
        return;
    }
    
    ecs_value_copy(world, &dst, value);

    ecs_modified(world, var, EcsScriptConstVar);
}

ecs_value_t biome_playerAttr_get(
    ecs_world_t *world,
    const char *name)
{
    ecs_entity_t player = ecs_lookup(world, "player");
    if (!player) {
        ecs_err("player scope not found");
        return (ecs_value_t){0};
    }

    ecs_entity_t var = ecs_lookup_child(world, player, name);
    if (!var) {
        ecs_err("player attribute '%s' not found", name);
        return (ecs_value_t){0};
    }

    return ecs_const_var_get(world, var);
}
