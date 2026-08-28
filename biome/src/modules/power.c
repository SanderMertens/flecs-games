#define BIOME_POWER_IMPL

#include "biome.h"

#include <stdlib.h>

ECS_COMPONENT_DECLARE(BiomePowerGrid);
ECS_COMPONENT_DECLARE(BiomePowerNetworkConsumers);

#define BiomePowerUnreached (UINT32_MAX)

static const int32_t biome_power_dx[] = {0, 1, 0, -1};
static const int32_t biome_power_dy[] = {-1, 0, 1, 0};

typedef struct biome_power_masks_t {
    uint64_t body;
    uint64_t source;
} biome_power_masks_t;

static void BiomePower_onSet(ecs_iter_t *it) {
    BiomePower *power = ecs_field(it, BiomePower, 0);

    for (int i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];

        if (power[i].producer) {
            ecs_set(it->world, e, BiomePowerProducer, {100}); /* Hardcoded for now, will be replaced later */
        }
        if (power[i].demand) {
            ecs_set(it->world, e, BiomePowerConsumer, {0});
        }
    }
}

static void UpdatePowerTint(ecs_iter_t *it) {
    BiomePowerConsumer *c = ecs_field(it, BiomePowerConsumer, 0);

    for (int i = 0; i < it->count; i ++) {
        bool owns_tint = ecs_owns(
            it->world, it->entities[i], FlecsTint);
        const FlecsTint *tint = owns_tint
            ? ecs_get(it->world, it->entities[i], FlecsTint)
            : NULL;
        if (c[i].powered) {
            if (tint && (tint->r != 0 || tint->g != 0 || tint->b != 0 ||
                tint->a != 0))
            {
                ecs_set(it->world, it->entities[i], FlecsTint,
                    {0, 0, 0, 0});
            }
        } else if (!tint || tint->r != 0 || tint->g != 0 || tint->b != 0 ||
            tint->a != 230)
        {
            ecs_set(it->world, it->entities[i], FlecsTint,
                {0, 0, 0, 230});
        }
    }
}

void biome_power_markDirty(ecs_world_t *world) {
    BiomePowerGrid *grid = ecs_get_mut(
        world, ecs_id(BiomePowerGrid), BiomePowerGrid);
    if (grid) {
        grid->dirty = true;
    }
}

static ecs_entity_t biome_power_terrain(ecs_world_t *world) {
    ecs_entity_t terrain = 0;
    ecs_iter_t it = ecs_each(world, FlecsTerrain);
    while (ecs_each_next(&it)) {
        for (int32_t i = 0; i < it.count; i ++) {
            if (!terrain) {
                terrain = it.entities[i];
            }
        }
    }
    return terrain;
}

static biome_power_masks_t biome_power_masks(
    ecs_world_t *world,
    BiomePowerGrid *grid)
{
    biome_power_masks_t masks = {0};

    ecs_iter_t it = ecs_query_iter(world, grid->prefabs);
    while (ecs_query_next(&it)) {
        BiomeBuildingBit *building_bit = ecs_field(
            &it, BiomeBuildingBit, 0);
        BiomePower *power = ecs_field(&it, BiomePower, 1);
        for (int32_t i = 0; i < it.count; i ++) {
            if (building_bit[i] < 0 || building_bit[i] >= 64) {
                continue;
            }

            uint64_t bit = 1llu << building_bit[i];
            if (power[i].conductor) {
                masks.body |= bit;
            }
            if (power[i].producer) {
                masks.body |= bit;
                masks.source |= bit;
            }
        }
    }

    return masks;
}

static void biome_power_push(ecs_vec_t *v, int32_t value) {
    *ecs_vec_append_t(NULL, v, int32_t) = value;
}

static void biome_power_stamp(
    TerrainPower *pow,
    int32_t idx,
    uint32_t gen,
    ecs_entity_t network)
{
    pow[idx].generation = gen;
    pow[idx].network = network;
    pow[idx].distance = BiomePowerUnreached;
}

static ecs_entity_t biome_power_networkRoot(ecs_world_t *world) {
    ecs_entity_t result = ecs_lookup(world, "scene.power");
    ecs_assert(result != 0, ECS_INTERNAL_ERROR, NULL);
    return result;
}

static void biome_power_clearNetworks(
    ecs_world_t *world,
    ecs_entity_t parent)
{
    ecs_vec_t entities;
    ecs_vec_init_t(NULL, &entities, ecs_entity_t, 0);
    ecs_iter_t it = ecs_children(world, parent);
    while (ecs_children_next(&it)) {
        for (int32_t i = 0; i < it.count; i ++) {
            if (ecs_has(world, it.entities[i], BiomePowerNetwork)) {
                *ecs_vec_append_t(NULL, &entities, ecs_entity_t) =
                    it.entities[i];
            }
        }
    }
    int32_t count = ecs_vec_count(&entities);
    ecs_entity_t *values = ecs_vec_first_t(&entities, ecs_entity_t);
    for (int32_t i = 0; i < count; i ++) {
        ecs_delete(world, values[i]);
    }
    ecs_vec_fini_t(NULL, &entities, ecs_entity_t);
}

static ecs_entity_t biome_power_networkNew(
    ecs_world_t *world,
    ecs_entity_t parent,
    int32_t index)
{
    char name[16];
    ecs_os_snprintf(name, sizeof(name), "%d", index + 1);
    ecs_entity_t entity = ecs_entity(world, {
        .parent = parent,
        .name = name
    });
    BiomePowerNetwork *network = ecs_ensure(
        world, entity, BiomePowerNetwork);
    network->production = 0;
    network->producer_count = 0;
    ecs_modified(world, entity, BiomePowerNetwork);
    return entity;
}

static void biome_power_flood(
    const TerrainOccupancy *occ,
    TerrainPower *pow,
    int32_t width,
    int32_t depth,
    uint32_t gen,
    ecs_entity_t network,
    biome_power_masks_t masks,
    int32_t x0,
    int32_t y0,
    int32_t fw,
    int32_t fh,
    ecs_vec_t *queue,
    ecs_vec_t *seeds)
{
    ecs_vec_clear(queue);
    ecs_vec_clear(seeds);

    for (int32_t y = y0; y < y0 + fh; y ++) {
        for (int32_t x = x0; x < x0 + fw; x ++) {
            int32_t idx = y * width + x;
            if (pow[idx].generation != gen) {
                biome_power_stamp(pow, idx, gen, network);
                biome_power_push(queue, idx);
            }
        }
    }

    int32_t head = 0;
    while (head < ecs_vec_count(queue)) {
        int32_t idx = ecs_vec_get_t(queue, int32_t, head)[0];
        head ++;

        if (occ[idx].buildings & masks.source) {
            biome_power_push(seeds, idx);
        }

        int32_t x = idx % width, y = idx / width;
        for (int32_t dir = 0; dir < 4; dir ++) {
            int32_t nx = x + biome_power_dx[dir];
            int32_t ny = y + biome_power_dy[dir];
            if (nx < 0 || ny < 0 || nx >= width || ny >= depth) {
                continue;
            }

            int32_t nidx = ny * width + nx;
            if (pow[nidx].generation == gen) {
                continue;
            }
            if (!(occ[nidx].buildings & masks.body)) {
                continue;
            }

            biome_power_stamp(pow, nidx, gen, network);
            biome_power_push(queue, nidx);
        }
    }

    ecs_vec_clear(queue);
    for (int32_t s = 0; s < ecs_vec_count(seeds); s ++) {
        int32_t idx = ecs_vec_get_t(seeds, int32_t, s)[0];
        pow[idx].distance = 0;
        biome_power_push(queue, idx);
    }

    head = 0;
    while (head < ecs_vec_count(queue)) {
        int32_t idx = ecs_vec_get_t(queue, int32_t, head)[0];
        head ++;

        uint32_t dist = pow[idx].distance;
        int32_t x = idx % width, y = idx / width;
        for (int32_t dir = 0; dir < 4; dir ++) {
            int32_t nx = x + biome_power_dx[dir];
            int32_t ny = y + biome_power_dy[dir];
            if (nx < 0 || ny < 0 || nx >= width || ny >= depth) {
                continue;
            }

            int32_t nidx = ny * width + nx;
            if (pow[nidx].generation != gen) {
                continue;
            }
            if (pow[nidx].network != network) {
                continue;
            }
            if (pow[nidx].distance != BiomePowerUnreached) {
                continue;
            }

            pow[nidx].distance = dist + 1;
            biome_power_push(queue, nidx);
        }
    }
}

static void biome_power_setPowered(
    ecs_world_t *world,
    ecs_entity_t e,
    bool powered,
    ecs_entity_t network)
{
    BiomePowerConsumer *c = ecs_get_mut(
        world, e, BiomePowerConsumer);
    if (c && c->powered == powered && c->network == network) {
        return;
    }
    if (!c) {
        ecs_set(world, e, BiomePowerConsumer, { powered, network });
        return;
    }
    c->powered = powered;
    c->network = network;
    ecs_modified(world, e, BiomePowerConsumer);
}

static int biome_power_consumerCmp(const void *a, const void *b) {
    const BiomePowerNetworkConsumer *ca = a;
    const BiomePowerNetworkConsumer *cb = b;
    if (ca->distance != cb->distance) {
        return ca->distance < cb->distance ? -1 : 1;
    }
    if (ca->entity != cb->entity) {
        return ca->entity < cb->entity ? -1 : 1;
    }
    return 0;
}

static bool biome_power_rebuild(
    ecs_world_t *world,
    BiomePowerGrid *grid,
    ecs_entity_t terrain,
    const FlecsTerrain *t)
{
    TerrainPower *pow = flecsEngine_terrain_getLayer(
        world, terrain, TerrainPowerIndex, TerrainPower);
    const TerrainOccupancy *occ = flecsEngine_terrain_getLayer(
        world, terrain, TerrainOccupancyIndex, TerrainOccupancy);
    if (!pow || !occ) {
        return false;
    }

    grid->generation ++;
    uint32_t gen = grid->generation;
    int32_t width = t->width, depth = t->depth;

    bool was_deferred = ecs_is_deferred(world);
    if (was_deferred) {
        ecs_defer_suspend(world);
    }

    ecs_entity_t network_root = biome_power_networkRoot(world);
    biome_power_clearNetworks(world, network_root);
    biome_power_masks_t masks = biome_power_masks(world, grid);

    ecs_vec_t queue, seeds;
    ecs_vec_init_t(NULL, &queue, int32_t, 0);
    ecs_vec_init_t(NULL, &seeds, int32_t, 0);

    int32_t network_count = 0;
    ecs_iter_t it = ecs_query_iter(world, grid->buildings);
    while (ecs_query_next(&it)) {
        FlecsTerrainPosition *tp = ecs_field(&it, FlecsTerrainPosition, 0);
        for (int32_t i = 0; i < it.count; i ++) {
            ecs_entity_t e = it.entities[i];
            const BiomePower *power = ecs_get(world, e, BiomePower);
            if (!power || !power->producer) {
                continue;
            }
            if (tp[i].terrain != terrain) {
                continue;
            }

            int32_t fw = tp[i].span_x ? tp[i].span_x : 1;
            int32_t fh = tp[i].span_y ? tp[i].span_y : 1;
            if (tp[i].x < 0 || tp[i].y < 0 ||
                (tp[i].x + fw) > width || (tp[i].y + fh) > depth)
            {
                continue;
            }

            int32_t idx = tp[i].y * width + tp[i].x;
            if (pow[idx].generation != gen) {
                ecs_entity_t network = biome_power_networkNew(
                    world, network_root, network_count ++);
                biome_power_flood(occ, pow, width, depth, gen, network,
                    masks, tp[i].x, tp[i].y, fw, fh, &queue, &seeds);
            }

            BiomePowerNetwork *network = ecs_get_mut(
                world, pow[idx].network, BiomePowerNetwork);
            const BiomePowerProducer *producer = ecs_get(
                world, e, BiomePowerProducer);
            if (producer) {
                network->production += producer->production;
            }
            network->producer_count ++;
            ecs_modified(
                world, pow[idx].network, BiomePowerNetwork);
        }
    }

    it = ecs_query_iter(world, grid->buildings);
    while (ecs_query_next(&it)) {
        FlecsTerrainPosition *tp = ecs_field(&it, FlecsTerrainPosition, 0);
        for (int32_t i = 0; i < it.count; i ++) {
            ecs_entity_t e = it.entities[i];
            const BiomePower *power = ecs_get(world, e, BiomePower);
            if (!power || power->demand <= 0) {
                continue;
            }
            if (tp[i].terrain != terrain) {
                continue;
            }

            int32_t fw = tp[i].span_x ? tp[i].span_x : 1;
            int32_t fh = tp[i].span_y ? tp[i].span_y : 1;
            if (tp[i].x < 0 || tp[i].y < 0 ||
                (tp[i].x + fw) > width || (tp[i].y + fh) > depth)
            {
                continue;
            }

            uint32_t best = BiomePowerUnreached;
            ecs_entity_t best_network = 0;

            for (int32_t y = tp[i].y; y < tp[i].y + fh; y ++) {
                for (int32_t x = tp[i].x; x < tp[i].x + fw; x ++) {
                    int32_t idx = y * width + x;
                    if (pow[idx].generation == gen &&
                        pow[idx].distance < best)
                    {
                        best = pow[idx].distance;
                        best_network = pow[idx].network;
                    }

                    for (int32_t dir = 0; dir < 4; dir ++) {
                        int32_t nx = x + biome_power_dx[dir];
                        int32_t ny = y + biome_power_dy[dir];
                        if (nx < 0 || ny < 0 || nx >= width || ny >= depth) {
                            continue;
                        }

                        int32_t nidx = ny * width + nx;
                        if (pow[nidx].generation != gen) {
                            continue;
                        }
                        if (pow[nidx].distance == BiomePowerUnreached) {
                            continue;
                        }
                        if ((pow[nidx].distance + 1) < best) {
                            best = pow[nidx].distance + 1;
                            best_network = pow[nidx].network;
                        }
                    }
                }
            }

            if (best != BiomePowerUnreached) {
                BiomePowerNetwork *network = ecs_get_mut(
                    world, best_network, BiomePowerNetwork);
                BiomePowerNetworkConsumer *c = ecs_vec_append_t(NULL,
                    &network->consumers, BiomePowerNetworkConsumer);
                c->entity = e;
                c->distance = best;
                c->demand = power->demand;
                ecs_modified(world, best_network, BiomePowerNetwork);
            } else {
                biome_power_setPowered(world, e, false, 0);
            }
        }
    }

    ecs_iter_t network_it = ecs_children(world, network_root);
    while (ecs_children_next(&network_it)) {
        for (int32_t i = 0; i < network_it.count; i ++) {
            BiomePowerNetwork *network = ecs_get_mut(
                world, network_it.entities[i], BiomePowerNetwork);
            if (!network) {
                continue;
            }
            int32_t consumer_count = ecs_vec_count(&network->consumers);
            if (consumer_count > 1) {
                qsort(ecs_vec_first_t(&network->consumers,
                    BiomePowerNetworkConsumer), (size_t)consumer_count,
                    sizeof(BiomePowerNetworkConsumer),
                    biome_power_consumerCmp);
                ecs_modified(
                    world, network_it.entities[i], BiomePowerNetwork);
            }
        }
    }

    ecs_vec_fini_t(NULL, &queue, int32_t);
    ecs_vec_fini_t(NULL, &seeds, int32_t);

    if (was_deferred) {
        ecs_defer_resume(world);
    }

    return true;
}

static void biome_power_distribute(
    ecs_world_t *world,
    BiomePowerGrid *grid)
{
    float total_production = 0;
    float total_demand = 0;
    float satisfied = 0;

    ecs_entity_t network_root = ecs_lookup(world, "scene.power");
    ecs_iter_t network_it = ecs_children(world, network_root);
    while (ecs_children_next(&network_it)) {
        for (int32_t n = 0; n < network_it.count; n ++) {
            const BiomePowerNetwork *network = ecs_get(
                world, network_it.entities[n], BiomePowerNetwork);
            if (!network) {
                continue;
            }
            float budget = network->production;

            int32_t consumer_count = ecs_vec_count(&network->consumers);
            const BiomePowerNetworkConsumer *consumers = ecs_vec_first_t(
                &network->consumers, BiomePowerNetworkConsumer);

            for (int32_t c = 0; c < consumer_count; c ++) {
                if (!ecs_is_alive(world, consumers[c].entity)) {
                    continue;
                }

                bool powered = budget >= consumers[c].demand;
                if (powered) {
                    budget -= consumers[c].demand;
                    satisfied += consumers[c].demand;
                }
                total_demand += consumers[c].demand;

                biome_power_setPowered(world, consumers[c].entity,
                    powered, network_it.entities[n]);
            }

            total_production += network->production;
        }
    }

    grid->total_production = total_production;
    grid->total_demand = total_demand;
    grid->satisfied_demand = satisfied;
}

void BiomePowerUpdate(ecs_iter_t *it) {
    ecs_world_t *world = it->world;
    BiomePowerGrid *grid = ecs_field(it, BiomePowerGrid, 0);

    ecs_entity_t terrain = biome_power_terrain(world);
    if (!terrain) {
        return;
    }

    const FlecsTerrain *t = ecs_get(world, terrain, FlecsTerrain);
    if (!t || t->width <= 0 || t->depth <= 0) {
        return;
    }
    if (ecs_vec_count(&t->layerTypes) <= TerrainPowerIndex) {
        return;
    }

    if (grid->dirty) {
        if (biome_power_rebuild(world, grid, terrain, t)) {
            grid->dirty = false;
        }
    }

    float production = grid->total_production;
    float demand = grid->total_demand;
    float satisfied = grid->satisfied_demand;

    biome_power_distribute(world, grid);

    /* Signalling the grid reinstantiates the widgets that read it, which is
     * the expensive part of the merge that follows. The totals only move when
     * a producer or consumer changes, so most frames have nothing to report. */
    if (grid->total_production != production ||
        grid->total_demand != demand ||
        grid->satisfied_demand != satisfied)
    {
        ecs_modified(world, ecs_id(BiomePowerGrid), BiomePowerGrid);
    }
}

static void BiomePowerPlacement(ecs_iter_t *it) {
    biome_power_markDirty(it->world);
}

void biomePowerImport(ecs_world_t *world) {
    ECS_MODULE(world, biomePower);

    ECS_IMPORT(world, biomeBuildings);

    ecs_set_name_prefix(world, "Biome");

    ECS_META_COMPONENT(world, BiomePower);
    ECS_META_COMPONENT(world, BiomePowerProducer);
    ECS_META_COMPONENT(world, BiomePowerConsumer);
    ECS_META_COMPONENT(world, TerrainPower);
    ECS_META_COMPONENT(world, BiomePowerNetworkConsumer);
    ecs_id(BiomePowerNetworkConsumers) = ecs_vector(world, {
        .entity = ecs_entity(world, {
            .name = "PowerNetworkConsumers",
            .symbol = "BiomePowerNetworkConsumers"
        }),
        .type = ecs_id(BiomePowerNetworkConsumer)
    });
    ECS_META_COMPONENT(world, BiomePowerNetwork);

    ECS_COMPONENT_DEFINE(world, BiomePowerGrid);

    ecs_struct(world, {
        .entity = ecs_id(BiomePowerGrid),
        .members = {
            { .name = "dirty", .type = ecs_id(ecs_bool_t),
                .offset = offsetof(BiomePowerGrid, dirty) },
            { .name = "generation", .type = ecs_id(ecs_u32_t),
                .offset = offsetof(BiomePowerGrid, generation) },
            { .name = "total_production", .type = ecs_id(ecs_f32_t),
                .offset = offsetof(BiomePowerGrid, total_production) },
            { .name = "total_demand", .type = ecs_id(ecs_f32_t),
                .offset = offsetof(BiomePowerGrid, total_demand) },
            { .name = "satisfied_demand", .type = ecs_id(ecs_f32_t),
                .offset = offsetof(BiomePowerGrid, satisfied_demand) }
        }
    });

    ecs_set_hooks(world, BiomePower, {
        .on_set = BiomePower_onSet
    });

    ecs_add_id(world, ecs_id(BiomePowerGrid), EcsSingleton);
    ecs_entity(world, {
        .name = "::scene.power",
        .root_sep = "::"
    });

    BiomePowerGrid *grid = ecs_singleton_ensure(world, BiomePowerGrid);
    grid->dirty = true;
    grid->buildings = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "buildingsQuery" }),
        .terms = {
            { .id = ecs_id(FlecsTerrainPosition) },
            { .id = ecs_id(BiomePower) }
        }
    });
    grid->prefabs = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "prefabsQuery" }),
        .terms = {
            { .id = ecs_id(BiomeBuildingBit) },
            { .id = ecs_id(BiomePower) },
            { .id = EcsPrefab }
        }
    });

    ecs_observer(world, {
        .query.terms = {
            { .id = ecs_id(FlecsTerrainPosition) },
            { .id = ecs_id(BiomePower) }
        },
        .events = { EcsOnSet, EcsOnRemove },
        .callback = BiomePowerPlacement
    });

    ECS_SYSTEM(world, BiomePowerUpdate, EcsPreUpdate,
        [inout] BiomePowerGrid);
    ecs_system_update(world, ecs_id(BiomePowerUpdate), &(ecs_system_desc_t){
        .immediate = true
    });

    ECS_SYSTEM(world, UpdatePowerTint, EcsPostUpdate,
        [in] BiomePowerConsumer);
}
