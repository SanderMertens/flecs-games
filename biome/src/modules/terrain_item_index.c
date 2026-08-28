#include "biome.h"

ECS_COMPONENT_DECLARE(TerrainItemIndex);

static void TerrainItemIndex_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    TerrainItemIndex *indices = ptr;
    for (int32_t i = 0; i < count; i ++) {
        ecs_map_init(&indices[i].records, NULL);
    }
}

static void TerrainItemIndex_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    TerrainItemIndex *indices = ptr;
    for (int32_t i = 0; i < count; i ++) {
        ecs_map_iter_t it = ecs_map_iter(&indices[i].records);
        while (ecs_map_next(&it)) {
            TerrainItemRecord *record = ecs_map_ptr(&it);
            ecs_vec_fini_t(NULL, &record->entities, ecs_entity_t);
            ecs_os_free(record);
        }
        ecs_map_fini(&indices[i].records);
    }
}

uint64_t biome_terrainItemIndex_pos(
    int32_t x,
    int32_t y)
{
    return (uint64_t)(uint32_t)x | ((uint64_t)(uint32_t)y << 32);
}

const TerrainItemRecord* biome_terrainItemIndex_get(
    const ecs_world_t *world,
    int32_t x,
    int32_t y)
{
    const TerrainItemIndex *index = ecs_singleton_get(
        world, TerrainItemIndex);
    if (!index) {
        return NULL;
    }

    return ecs_map_get_deref(
        &index->records, TerrainItemRecord,
        (ecs_map_key_t)biome_terrainItemIndex_pos(x, y));
}

static TerrainItemRecord* biome_terrain_item_index_ensure(
    TerrainItemIndex *index,
    int32_t x,
    int32_t y)
{
    ecs_map_key_t pos = (ecs_map_key_t)biome_terrainItemIndex_pos(x, y);
    TerrainItemRecord *record = ecs_map_get_deref(
        &index->records, TerrainItemRecord, pos);
    if (!record) {
        record = ecs_map_insert_alloc_t(
            &index->records, TerrainItemRecord, pos);
        ecs_vec_init_t(NULL, &record->entities, ecs_entity_t, 0);
    }
    return record;
}

void biome_terrainItemIndex_place(
    ecs_world_t *world,
    ecs_entity_t entity)
{
    const FlecsTerrainPosition *tp = ecs_get(
        world, entity, FlecsTerrainPosition);
    if (!tp) {
        return;
    }

    TerrainItemIndex *index = ecs_singleton_ensure(world, TerrainItemIndex);
    int32_t width = tp->span_x ? tp->span_x : 1;
    int32_t height = tp->span_y ? tp->span_y : 1;

    bool changed = false;
    for (int32_t y = tp->y; y < tp->y + height; y ++) {
        for (int32_t x = tp->x; x < tp->x + width; x ++) {
            TerrainItemRecord *record = biome_terrain_item_index_ensure(
                index, x, y);
            int32_t count = ecs_vec_count(&record->entities);
            ecs_entity_t *entities = ecs_vec_first_t(
                &record->entities, ecs_entity_t);
            bool found = false;
            for (int32_t i = 0; i < count; i ++) {
                if (entities[i] == entity) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                *ecs_vec_append_t(
                    NULL, &record->entities, ecs_entity_t) = entity;
                changed = true;
            }
        }
    }

    if (changed) {
        ecs_singleton_modified(world, TerrainItemIndex);
    }
}

void biome_terrainItemIndex_remove(
    ecs_world_t *world,
    ecs_entity_t entity)
{
    const FlecsTerrainPosition *tp = ecs_get(
        world, entity, FlecsTerrainPosition);
    TerrainItemIndex *index = ecs_singleton_get_mut(
        world, TerrainItemIndex);
    if (!tp || !index) {
        return;
    }

    int32_t width = tp->span_x ? tp->span_x : 1;
    int32_t height = tp->span_y ? tp->span_y : 1;

    bool changed = false;
    for (int32_t y = tp->y; y < tp->y + height; y ++) {
        for (int32_t x = tp->x; x < tp->x + width; x ++) {
            ecs_map_key_t pos = (ecs_map_key_t)biome_terrainItemIndex_pos(x, y);
            TerrainItemRecord *record = ecs_map_get_deref(
                &index->records, TerrainItemRecord, pos);
            if (!record) {
                continue;
            }

            int32_t count = ecs_vec_count(&record->entities);
            ecs_entity_t *entities = ecs_vec_first_t(
                &record->entities, ecs_entity_t);
            for (int32_t i = 0; i < count; i ++) {
                if (entities[i] == entity) {
                    ecs_vec_remove_t(&record->entities, ecs_entity_t, i);
                    changed = true;
                    break;
                }
            }

            if (!ecs_vec_count(&record->entities)) {
                ecs_vec_fini_t(NULL, &record->entities, ecs_entity_t);
                ecs_map_remove_free(&index->records, pos);
            }
        }
    }

    if (changed) {
        ecs_singleton_modified(world, TerrainItemIndex);
    }
}

void biomeTerrainItemIndexImport(ecs_world_t *world) {
    ECS_MODULE(world, biomeTerrainItemIndex);

    ecs_set_name_prefix(world, "Biome");

    ECS_COMPONENT_DEFINE(world, TerrainItemIndex);
    ecs_set_hooks(world, TerrainItemIndex, {
        .ctor = TerrainItemIndex_ctor,
        .dtor = TerrainItemIndex_dtor
    });

    ecs_add_id(world, ecs_id(TerrainItemIndex), EcsSingleton);
    ecs_singleton_ensure(world, TerrainItemIndex);
}
