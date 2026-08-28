#define BIOME_RESOURCES_IMPL

#include "biome.h"

ECS_COMPONENT_DECLARE(BiomeResourceStorageMap);
ECS_COMPONENT_DECLARE(biome_resource_storageKind_t);

int32_t biome_resource_playerAmount(
    ecs_world_t *world,
    const char *attribute,
    ecs_entity_t resource)
{
    ecs_value_t value = biome_playerAttr_get(world, attribute);
    if (!value.ptr) {
        return 0;
    }
    int32_t *amount = (int32_t*)ecs_map_get(value.ptr, resource);
    return amount ? *amount : 0;
}

bool biome_resource_playerAdd(
    ecs_world_t *world,
    const char *attribute,
    ecs_entity_t resource,
    int32_t amount)
{
    ecs_value_t value = biome_playerAttr_get(world, attribute);
    if (!value.ptr) {
        return false;
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

    biome_playerAttr_set(world, attribute, &(ecs_value_t) {
        .type = ecs_id(BiomeResourceStorageMap),
        .ptr = &next
    });
    ecs_map_fini(&next);
    return true;
}

static void biome_resource_addCapacity(
    ecs_world_t *world,
    int32_t amount)
{
    ecs_value_t value = biome_playerAttr_get(
        world, "ResourceCapacityTotals");
    if (!value.ptr) {
        return;
    }

    ecs_map_t next = {0};
    ecs_map_init(&next, NULL);
    ecs_map_copy(&next, value.ptr);
    ecs_map_iter_t it = ecs_map_iter(&next);
    while (ecs_map_next(&it)) {
        int32_t *capacity = (int32_t*)ecs_map_get(
            &next, ecs_map_key(&it));
        int64_t updated = (int64_t)*capacity + amount;
        if (updated < 0) {
            updated = 0;
        } else if (updated > INT32_MAX) {
            updated = INT32_MAX;
        }
        *capacity = (int32_t)updated;
    }

    biome_playerAttr_set(world, "ResourceCapacityTotals",
        &(ecs_value_t) {
            .type = ecs_id(BiomeResourceStorageMap),
            .ptr = &next
        });
    ecs_map_fini(&next);
}

static void BiomePlayerStoragePlaced(ecs_iter_t *it) {
    int32_t direction = it->event == EcsOnRemove ? -1 : 1;

    for (int32_t i = 0; i < it->count; i ++) {
        const BiomePlayerStorage *storage = ecs_get(
            it->world, it->entities[i], BiomePlayerStorage);
        if (!storage) {
            continue;
        }
        biome_resource_addCapacity(
            it->world, storage->capacity * direction);

        if (direction < 0 || !ecs_map_is_init(&storage->resources)) {
            continue;
        }

        ecs_map_iter_t mit = ecs_map_iter(&storage->resources);
        while (ecs_map_next(&mit)) {
            biome_resource_playerAdd(
                it->world, "ResourceTotals", ecs_map_key(&mit),
                (int32_t)ecs_map_value(&mit));
        }
    }
}

void biomeResourcesImport(ecs_world_t *world) {
    ECS_MODULE(world, biomeResources);

    ecs_id(BiomeResourceStorageMap) = ecs_map_type(world, {
        .entity = ecs_entity(world, {
            .name = "StorageMap",
            .symbol = "BiomeResourceStorageMap"
        }),
        .key_type = ecs_id(ecs_entity_t),
        .type = ecs_id(ecs_i32_t)
    });

    ecs_id(biome_resource_storageKind_t) = ecs_enum(world, {
        .entity = ecs_entity(world, {
            .name = "StorageKind",
            .symbol = "biome_resource_storageKind_t"
        }),
        .constants = {
            {"Source", BiomeResourceStorageKindSource},
            {"Sink", BiomeResourceStorageKindSink},
            {"Factory", BiomeResourceStorageKindFactory}
        }
    });

    ecs_set_name_prefix(world, "Biome");
    ECS_META_COMPONENT(world, BiomeResource);
    ECS_META_COMPONENT(world, BiomeRecipe);
    ECS_META_COMPONENT(world, BiomePlayerStorage);

    ecs_set_name_prefix(world, "BiomeResource");
    ECS_META_COMPONENT(world, BiomeResourceStorageDesc);
    ECS_META_COMPONENT(world, BiomeResourceStorage);

    ecs_observer(world, {
        .query.terms = {
            { ecs_id(BiomePlayerStorage) }
        },
        .events = { EcsOnSet, EcsOnRemove },
        .callback = BiomePlayerStoragePlaced
    });
}
