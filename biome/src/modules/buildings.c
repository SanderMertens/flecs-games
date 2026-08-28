#define BIOME_BUILDINGS_IMPL

#include "biome.h"

ECS_COMPONENT_DECLARE(BiomeBuildingBit);
ECS_COMPONENT_DECLARE(BiomeBuildingRequirementMask);

static int8_t biome_last_building_bit = -1;

static void BiomeBuildingBitOnAdd(ecs_iter_t *it) {
    BiomeBuildingBit *bits = ecs_field(it, BiomeBuildingBit, 0);

    for (int32_t i = 0; i < it->count; i ++) {
        ecs_assert(biome_last_building_bit < 63, ECS_OUT_OF_RANGE,
            "cannot assign more than 64 building bits");
        bits[i] = ++ biome_last_building_bit;
    }
}

static void biome_buildings_occupancy(
    ecs_iter_t *it,
    bool occupied)
{
    ecs_world_t *world = it->world;
    FlecsTerrainPosition *tp = ecs_field(it, FlecsTerrainPosition, 0);

    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        const BiomeBuildingBit *bit = ecs_get(world, e, BiomeBuildingBit);
        if (!bit || *bit < 0 || *bit >= 64) {
            continue;
        }
        if (!tp[i].terrain || !ecs_is_alive(world, tp[i].terrain)) {
            continue;
        }

        const FlecsTerrain *t = ecs_get(world, tp[i].terrain, FlecsTerrain);
        if (!t || ecs_vec_count(&t->layerTypes) <= TerrainOccupancyIndex) {
            continue;
        }

        TerrainOccupancy *occ = flecsEngine_terrain_getLayer(
            world, tp[i].terrain, TerrainOccupancyIndex, TerrainOccupancy);
        if (!occ) {
            continue;
        }

        uint64_t mask = 1llu << *bit;
        int32_t fw = tp[i].span_x ? tp[i].span_x : 1;
        int32_t fh = tp[i].span_y ? tp[i].span_y : 1;

        for (int32_t cy = tp[i].y; cy < tp[i].y + fh; cy ++) {
            if (cy < 0 || cy >= t->depth) {
                continue;
            }
            for (int32_t cx = tp[i].x; cx < tp[i].x + fw; cx ++) {
                if (cx < 0 || cx >= t->width) {
                    continue;
                }
                if (occupied) {
                    occ[cy * t->width + cx].buildings |= mask;
                } else {
                    occ[cy * t->width + cx].buildings &= ~mask;
                }
            }
        }

        BiomeBuildingOccupancyChanged event = {
            .terrain = tp[i].terrain,
            .x = tp[i].x,
            .y = tp[i].y,
            .width = fw,
            .height = fh,
            .building_bit = *bit,
            .occupied = occupied
        };
        ecs_emit(world, &(ecs_event_desc_t) {
            .event = ecs_id(BiomeBuildingOccupancyChanged),
            .entity = EcsAny,
            .param = &event
        });
    }
}

static void BiomeBuildingOnSet(ecs_iter_t *it) {
    BiomeBuilding *buildings = ecs_field(it, BiomeBuilding, 0);

    for (int32_t i = 0; i < it->count; i ++) {
        int32_t count = ecs_vec_count(&buildings[i].requires);
        if (!count) {
            if (ecs_owns(it->world, it->entities[i],
                BiomeBuildingRequirementMask))
            {
                ecs_remove(it->world, it->entities[i],
                    BiomeBuildingRequirementMask);
            }
            continue;
        }

        uint64_t mask = 0;
        ecs_entity_t *requirements = ecs_vec_first_t(
            &buildings[i].requires, ecs_entity_t);
        for (int32_t r = 0; r < count; r ++) {
            const BiomeBuildingBit *bit = ecs_get(
                it->world, requirements[r], BiomeBuildingBit);
            if (!bit || *bit < 0 || *bit >= 64) {
                ecs_warn("building '%s' has invalid requirement '%s'",
                    ecs_get_name(it->world, it->entities[i]),
                    ecs_get_name(it->world, requirements[r]));
                continue;
            }
            mask |= 1llu << *bit;
        }

        ecs_set(it->world, it->entities[i],
            BiomeBuildingRequirementMask, { mask });
    }
}

static void BiomeTerrainPositionOnSet(ecs_iter_t *it) {
    if (it->event_id != ecs_id(FlecsTerrainPosition)) {
        return;
    }
    biome_buildings_occupancy(it, true);
}

static void BiomeTerrainPositionOnRemove(ecs_iter_t *it) {
    if (it->event_id != ecs_id(FlecsTerrainPosition)) {
        return;
    }
    biome_buildings_occupancy(it, false);
}

void biomeBuildingsImport(ecs_world_t *world) {
    ECS_MODULE(world, biomeBuildings);

    ecs_set_name_prefix(world, "Biome");
    ECS_META_COMPONENT(world, BiomeBuildingSpawn);
    ECS_META_COMPONENT(world, BiomeBuilding);
    ECS_META_COMPONENT(world, BiomeBuildingOccupancyChanged);
    ecs_id(BiomeBuildingBit) = ecs_primitive(world, {
        .entity = ecs_entity(world, {
            .name = "BuildingBit",
            .symbol = "BiomeBuildingBit"
        }),
        .kind = EcsI8
    });
    ecs_id(BiomeBuildingRequirementMask) = ecs_primitive(world, {
        .entity = ecs_entity(world, {
            .name = "BuildingRequirementMask",
            .symbol = "BiomeBuildingRequirementMask"
        }),
        .kind = EcsU64
    });
    ecs_set_hooks(world, BiomeBuilding, {
        .ctor = flecs_default_ctor,
        .on_set = BiomeBuildingOnSet
    });
    ecs_set_hooks(world, BiomeBuildingBit, {
        .on_add = BiomeBuildingBitOnAdd
    });

    ecs_add_pair(world, ecs_id(BiomeBuildingBit),
        EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(BiomeBuildingRequirementMask),
        EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(BiomeBuilding), EcsWith,
        ecs_id(BiomeBuildingBit));

    ecs_observer(world, {
        .query.terms = {
            { .id = ecs_id(FlecsTerrainPosition) },
            { .id = ecs_id(BiomeBuilding) }
        },
        .events = { EcsOnSet },
        .callback = BiomeTerrainPositionOnSet
    });

    ecs_observer(world, {
        .query.terms = {
            { .id = ecs_id(FlecsTerrainPosition) },
            { .id = ecs_id(BiomeBuilding) }
        },
        .events = { EcsOnRemove },
        .callback = BiomeTerrainPositionOnRemove
    });
}
