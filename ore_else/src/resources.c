#define ORE_ELSE_RESOURCES_IMPL
#include "ore_else.h"

ECS_COMPONENT_DECLARE(OreStorageMap);

ecs_value_t orePlayerAttrGet(
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

    return ecs_mut_var_get(world, var);
}

static void orePlayerAttrSet(
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

    ecs_value_t dst = ecs_mut_var_get(world, var);
    if (!dst.ptr) {
        ecs_err("player attribute '%s' has no value", name);
        return;
    }

    if (ecs_value_equals(world, &dst, value)) {
        return;
    }

    ecs_value_copy(world, &dst, value);

    ecs_mut_var_modified(world, var);
}

int32_t oreInventoryGet(
    ecs_world_t *world,
    ecs_entity_t resource)
{
    ecs_value_t value = orePlayerAttrGet(world, "Inventory");
    if (!value.ptr) {
        return 0;
    }

    int32_t *amount = (int32_t*)ecs_map_get(value.ptr, resource);
    return amount ? *amount : 0;
}

int32_t oreInventoryKinds(ecs_world_t *world) {
    ecs_value_t value = orePlayerAttrGet(world, "Inventory");
    if (!value.ptr) {
        return 0;
    }

    int32_t kinds = 0;

    ecs_map_iter_t it = ecs_map_iter(value.ptr);
    while (ecs_map_next(&it)) {
        if ((int32_t)ecs_map_value(&it) > 0) {
            kinds ++;
        }
    }

    return kinds;
}

bool oreInventoryFits(
    ecs_world_t *world,
    const ecs_entity_t *items,
    const int32_t *amounts,
    int32_t count)
{
    int32_t kinds = oreInventoryKinds(world);

    for (int32_t i = 0; i < count; i ++) {
        if ((amounts && amounts[i] <= 0) || oreInventoryGet(world, items[i]) > 0) {
            continue;
        }

        bool seen = false;
        for (int32_t j = 0; j < i; j ++) {
            seen = seen || (items[j] == items[i] && (!amounts || amounts[j] > 0));
        }

        kinds += !seen;
    }

    return kinds <= ORE_STOCK_SLOTS;
}

static void oreInventoryFull(ecs_world_t *world) {
    oreToast(world, "Inventory full - 15 item kinds is the limit", true);
}

bool oreInventoryAdd(
    ecs_world_t *world,
    ecs_entity_t resource,
    int32_t amount)
{
    ecs_value_t value = orePlayerAttrGet(world, "Inventory");
    if (!value.ptr) {
        return false;
    }

    if (amount > 0 && oreInventoryGet(world, resource) <= 0) {
        if (oreInventoryKinds(world) >= ORE_STOCK_SLOTS) {
            oreInventoryFull(world);
            return false;
        }
    }

    ecs_map_t next = {0};
    ecs_map_init(&next, NULL);
    ecs_map_copy(&next, value.ptr);

    int32_t *stored = (int32_t*)ecs_map_ensure(&next, resource);
    int64_t updated = (int64_t)*stored + amount;
    if (updated < 0 || updated > INT32_MAX) {
        ecs_map_fini(&next);
        return false;
    }

    *stored = (int32_t)updated;

    orePlayerAttrSet(world, "Inventory", &(ecs_value_t) {
        .type = ecs_id(OreStorageMap),
        .ptr = &next
    });

    ecs_map_fini(&next);
    return true;
}

void oreResourcesImport(ecs_world_t *world) {
    ecs_id(OreStorageMap) = ecs_map_type(world, {
        .entity = ecs_entity(world, {
            .name = "StorageMap",
            .symbol = "OreStorageMap"
        }),
        .key_type = ecs_id(ecs_entity_t),
        .type = ecs_id(ecs_i32_t)
    });

    ECS_META_COMPONENT(world, OreResource);
    ECS_META_COMPONENT(world, OreIcon);
    ECS_META_COMPONENT(world, OreRecipe);
    ECS_META_COMPONENT(world, OreDeposit);

    ecs_add_pair(world, ecs_id(OreIcon), EcsOnInstantiate, EcsInherit);
}
