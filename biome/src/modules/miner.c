#define BIOME_MINER_IMPL

#include "biome.h"

typedef int32_t BiomeResourceMinerFrames;

ECS_COMPONENT_DECLARE(BiomeResourceMinerFrames);

static ecs_entity_t biome_miner_findDeposit(
    const ecs_world_t *world,
    const FlecsTerrainPosition *position)
{
    const TerrainItemRecord *record = biome_terrainItemIndex_get(
        world, position->x, position->y);
    if (!record) {
        return 0;
    }

    int32_t count = ecs_vec_count(&record->entities);
    ecs_entity_t *entities = ecs_vec_first_t(
        &record->entities, ecs_entity_t);
    for (int32_t i = 0; i < count; i ++) {
        ecs_entity_t candidate = entities[i];
        if (!ecs_is_alive(world, candidate) ||
            !ecs_has(world, candidate, BiomeResourceDeposit))
        {
            continue;
        }

        const FlecsTerrainPosition *candidate_position = ecs_get(
            world, candidate, FlecsTerrainPosition);
        if (!candidate_position ||
            candidate_position->terrain != position->terrain)
        {
            continue;
        }

        return candidate;
    }

    return 0;
}

static void BiomeResourceDepositPlace(ecs_iter_t *it) {
    if (it->event_id != ecs_id(FlecsTerrainPosition)) {
        return;
    }

    for (int32_t i = 0; i < it->count; i ++) {
        biome_terrainItemIndex_place(it->world, it->entities[i]);
    }
}

static void BiomeResourceMinerPlace(ecs_iter_t *it) {
    if (it->event_id != ecs_id(FlecsTerrainPosition)) {
        return;
    }

    const FlecsTerrainPosition *positions = ecs_field(
        it, FlecsTerrainPosition, 0);
    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t entity = it->entities[i];
        ecs_entity_t deposit = biome_miner_findDeposit(
            it->world, &positions[i]);

        ecs_set(it->world, entity, BiomeResourceMiner, { deposit });
        ecs_set(it->world, entity, BiomeResourceMinerFrames, { 0 });
    }
}

static bool biome_miner_isActive(
    const ecs_world_t *world,
    const BiomeResourceMiner *miner,
    const BiomeResourceStorage *storage,
    const BiomeResourceStorageDesc *desc,
    const BiomePowerConsumer *power)
{
    if (!power->powered ||
        desc->kind != BiomeResourceStorageKindSource ||
        desc->capacity <= 0 || !miner->deposit ||
        !ecs_is_alive(world, miner->deposit))
    {
        return false;
    }

    const BiomeResourceDeposit *deposit = ecs_get(
        world, miner->deposit, BiomeResourceDeposit);
    if (!deposit || !deposit->resource || deposit->amount <= 0) {
        return false;
    }

    const BiomeResource *resource = ecs_get(
        world, deposit->resource, BiomeResource);
    if (!resource || resource->mine_amount <= 0) {
        return false;
    }

    if (!ecs_map_is_init(&storage->resources)) {
        return true;
    }

    const int32_t *stored = (const int32_t*)ecs_map_get(
        &storage->resources, (ecs_map_key_t)deposit->resource);
    return !stored || *stored < desc->capacity;
}

static void BiomeResourceMinerUpdateEmitter(ecs_iter_t *it) {
    const BiomeResourceMiner *miners = ecs_field(
        it, BiomeResourceMiner, 0);
    const BiomeResourceStorage *storages = ecs_field(
        it, BiomeResourceStorage, 1);
    const BiomeResourceStorageDesc *descs = ecs_field(
        it, BiomeResourceStorageDesc, 2);
    const BiomePowerConsumer *power = ecs_field(
        it, BiomePowerConsumer, 3);
    FlecsParticleEmitter *emitters = ecs_field(
        it, FlecsParticleEmitter, 4);

    for (int32_t i = 0; i < it->count; i ++) {
        emitters[i].enabled = biome_miner_isActive(
            it->world, &miners[i], &storages[i], &descs[i], &power[i]);
    }
}

static void biome_miner_postPickupRequests(
    ecs_world_t *world,
    ecs_entity_t miner,
    ecs_entity_t resource_entity,
    const BiomeResource *resource,
    BiomeResourceStorage *storage,
    int32_t stored)
{
    int32_t pickup_amount = resource->min_drone_amount;
    if (pickup_amount <= 0) {
        return;
    }

    int32_t reserved_amount = 0;
    int32_t *reserved = NULL;
    if (ecs_map_is_init(&storage->reserved)) {
        reserved = (int32_t*)ecs_map_get(
            &storage->reserved, (ecs_map_key_t)resource_entity);
        if (reserved) {
            reserved_amount = *reserved;
        }
    }

    while ((int64_t)stored - reserved_amount >= pickup_amount) {
        if (!reserved) {
            if (!ecs_map_is_init(&storage->reserved)) {
                ecs_map_init(&storage->reserved, NULL);
            }
            reserved = (int32_t*)ecs_map_ensure(
                &storage->reserved, (ecs_map_key_t)resource_entity);
        }

        reserved_amount += pickup_amount;
        *reserved = reserved_amount;

        biome_logistics_postRequest(world, BiomeRequestPickup,
            miner, resource_entity, pickup_amount, 0);
    }
}

static void BiomeResourceMinerUpdate(ecs_iter_t *it) {
    const BiomeResourceMiner *miners = ecs_field(
        it, BiomeResourceMiner, 0);
    BiomeResourceStorage *storages = ecs_field(
        it, BiomeResourceStorage, 1);
    const BiomeResourceStorageDesc *descs = ecs_field(
        it, BiomeResourceStorageDesc, 2);
    BiomeResourceMinerFrames *frames = ecs_field(
        it, BiomeResourceMinerFrames, 3);
    const BiomePowerConsumer *power = ecs_field(
        it, BiomePowerConsumer, 4);

    for (int32_t i = 0; i < it->count; i ++) {
        if (!power[i].powered ||
            descs[i].kind != BiomeResourceStorageKindSource)
        {
            continue;
        }

        ecs_entity_t deposit_entity = miners[i].deposit;
        if (!deposit_entity ||
            !ecs_is_alive(it->world, deposit_entity))
        {
            continue;
        }

        const BiomeResourceDeposit *deposit = ecs_get(
            it->world, deposit_entity, BiomeResourceDeposit);
        if (!deposit || !deposit->resource || deposit->amount <= 0) {
            continue;
        }

        const BiomeResource *resource = ecs_get(
            it->world, deposit->resource, BiomeResource);
        if (!resource) {
            continue;
        }

        int32_t mine_time = resource->mine_time;
        if (mine_time < 1) {
            mine_time = 1;
        }

        frames[i] ++;
        if (frames[i] < mine_time) {
            continue;
        }
        frames[i] = 0;

        if (resource->mine_amount <= 0 || descs[i].capacity <= 0) {
            continue;
        }

        BiomeResourceStorage *storage = &storages[i];
        if (!ecs_map_is_init(&storage->resources)) {
            ecs_map_init(&storage->resources, NULL);
        }

        int32_t *stored = (int32_t*)ecs_map_ensure(
            &storage->resources, (ecs_map_key_t)deposit->resource);
        int32_t room = descs[i].capacity - *stored;
        if (room <= 0) {
            continue;
        }

        int32_t amount = resource->mine_amount;
        if (amount > deposit->amount) {
            amount = deposit->amount;
        }
        if (amount > room) {
            amount = room;
        }

        *stored += amount;
        biome_miner_postPickupRequests(it->world, it->entities[i],
            deposit->resource, resource, storage, *stored);

        BiomeResourceDeposit updated_deposit = *deposit;
        updated_deposit.amount -= amount;
        ecs_set_ptr(it->world, deposit_entity,
            BiomeResourceDeposit, &updated_deposit);
        ecs_modified_id(it->world, it->entities[i],
            ecs_id(BiomeResourceStorage));
    }
}

void biomeMinerImport(ecs_world_t *world) {
    ECS_MODULE(world, biomeMiner);

    ECS_IMPORT(world, biomeResources);
    ECS_IMPORT(world, biomeLogistics);
    ECS_IMPORT(world, biomeTerrainItemIndex);

    ecs_set_name_prefix(world, "BiomeResource");
    ECS_META_COMPONENT(world, BiomeResourceDeposit);
    ECS_META_COMPONENT(world, BiomeResourceMiner);

    ecs_set_name_prefix(world, "BiomeResourceMiner");
    ECS_COMPONENT_DEFINE(world, BiomeResourceMinerFrames);

    ecs_observer(world, {
        .query.terms = {
            { .id = ecs_id(FlecsTerrainPosition) },
            { .id = ecs_id(BiomeResourceDeposit) }
        },
        .events = { EcsOnSet },
        .callback = BiomeResourceDepositPlace
    });

    ecs_observer(world, {
        .query.terms = {
            { .id = ecs_id(FlecsTerrainPosition) },
            { .id = ecs_id(BiomeResourceMiner) }
        },
        .events = { EcsOnSet },
        .callback = BiomeResourceMinerPlace
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "UpdateEmitter" }),
        .query.terms = {
            { .id = ecs_id(BiomeResourceMiner), .inout = EcsIn },
            { .id = ecs_id(BiomeResourceStorage), .inout = EcsIn },
            { .id = ecs_id(BiomeResourceStorageDesc), .inout = EcsIn },
            { .id = ecs_id(BiomePowerConsumer), .inout = EcsIn },
            { .id = ecs_id(FlecsParticleEmitter) }
        },
        .phase = EcsPreUpdate,
        .callback = BiomeResourceMinerUpdateEmitter
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "Update" }),
        .query.terms = {
            { .id = ecs_id(BiomeResourceMiner), .inout = EcsIn },
            { .id = ecs_id(BiomeResourceStorage) },
            { .id = ecs_id(BiomeResourceStorageDesc), .inout = EcsIn },
            { .id = ecs_id(BiomeResourceMinerFrames) },
            { .id = ecs_id(BiomePowerConsumer), .inout = EcsIn }
        },
        .phase = EcsOnUpdate,
        .callback = BiomeResourceMinerUpdate
    });
}
