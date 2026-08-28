#include "ore_else.h"

ECS_TAG_DECLARE(OreQualityCycle);

void oreQualityClick(ecs_world_t *world, ecs_entity_t widget) {
    if (!ecs_has(world, widget, OreQualityCycle)) {
        return;
    }

    ecs_entity_t var = ecs_lookup(world, "renderQuality");
    if (!var) {
        ecs_err("mut variable renderQuality not found");
        return;
    }

    ecs_value_t value = ecs_mut_var_get(world, var);
    const EcsConstants *tiers = ecs_get(world, value.type, EcsConstants);
    if (!value.ptr || !tiers) {
        return;
    }

    int32_t count = ecs_vec_count(&tiers->ordered_constants);
    if (count <= 0) {
        return;
    }

    int32_t *tier = value.ptr;
    *tier = (*tier + count - 1) % count;

    ecs_mut_var_modified(world, var);
}

void oreQualityImport(ecs_world_t *world) {
    ECS_TAG_DEFINE(world, OreQualityCycle);

    ecs_add_pair(world, OreQualityCycle, EcsWith, ecs_id(FlecsUiWidgetState));
}
