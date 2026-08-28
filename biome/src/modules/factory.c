#define BIOME_FACTORY_IMPL

#include "biome.h"

typedef struct BiomeFactoryProgress {
    float remaining;
    float total;
} BiomeFactoryProgress;

ECS_COMPONENT_DECLARE(BiomeFactoryProgress);
ECS_COMPONENT_DECLARE(biome_factory_outputMode_t);
ECS_COMPONENT_DECLARE(biome_factory_inputMode_t);

static int32_t biome_factory_mapValue(
    const BiomeResourceStorageMap *map,
    ecs_entity_t resource)
{
    if (!ecs_map_is_init(map)) {
        return 0;
    }
    const int32_t *value = (const int32_t*)ecs_map_get(
        map, (ecs_map_key_t)resource);
    return value ? *value : 0;
}

static int32_t *biome_factory_mapEnsure(
    BiomeResourceStorageMap *map,
    ecs_entity_t resource)
{
    if (!ecs_map_is_init(map)) {
        ecs_map_init(map, NULL);
    }
    return (int32_t*)ecs_map_ensure(map, (ecs_map_key_t)resource);
}

static bool biome_factory_combineRequest(
    ecs_world_t *world,
    ecs_entity_t factory,
    ecs_entity_t resource,
    int32_t amount,
    int32_t max_amount,
    const BiomeResourceStorage *storage)
{
    int32_t count = ecs_vec_count(&storage->outstanding_requests);
    const ecs_entity_t *requests = ecs_vec_first_t(
        &storage->outstanding_requests, ecs_entity_t);
    for (int32_t i = 0; i < count; i ++) {
        ecs_entity_t request_entity = requests[i];
        if (!ecs_is_alive(world, request_entity)) {
            continue;
        }

        ecs_entity_t request_kind = ecs_get_target(
            world, request_entity, ecs_id(BiomeLogisticsRequest), 0);
        ecs_id_t request_id = ecs_pair(
            ecs_id(BiomeLogisticsRequest), request_kind);
        const BiomeLogisticsRequest *request = request_kind
            ? ecs_get_id(world, request_entity, request_id)
            : NULL;
        if (!request || request->kind != BiomeRequestDropOff ||
            request->source != factory || request->resource != resource ||
            amount > max_amount - request->amount)
        {
            continue;
        }

        BiomeLogisticsRequest *request_mut = ecs_get_mut_id(
            world, request_entity, request_id);
        request_mut->amount += amount;
        ecs_modified_id(world, request_entity, request_id);
        return true;
    }
    return false;
}

static bool biome_factory_consumeInputs(
    const BiomeRecipe *recipe,
    BiomeResourceStorage *storage)
{
    if (!ecs_map_is_init(&recipe->inputs)) {
        return true;
    }

    ecs_map_iter_t it = ecs_map_iter(&recipe->inputs);
    while (ecs_map_next(&it)) {
        int32_t amount = (int32_t)ecs_map_value(&it);
        if (amount > 0 && biome_factory_mapValue(
            &storage->resources, ecs_map_key(&it)) < amount)
        {
            return false;
        }
    }

    it = ecs_map_iter(&recipe->inputs);
    while (ecs_map_next(&it)) {
        int32_t amount = (int32_t)ecs_map_value(&it);
        if (amount > 0) {
            int32_t *stored = biome_factory_mapEnsure(
                &storage->resources, ecs_map_key(&it));
            *stored -= amount;
        }
    }

    return true;
}

static bool biome_factory_captureInputs(
    ecs_world_t *world,
    const BiomeRecipe *recipe)
{
    if (!ecs_map_is_init(&recipe->inputs)) {
        return true;
    }

    float required = 0;
    ecs_map_iter_t it = ecs_map_iter(&recipe->inputs);
    while (ecs_map_next(&it)) {
        int32_t amount = (int32_t)ecs_map_value(&it);
        if (amount <= 0) {
            continue;
        }
        const BiomeResource *resource = ecs_get(
            world, ecs_map_key(&it), BiomeResource);
        if (!resource) {
            return false;
        }
        required += resource->greenhouse_gas * (float)amount;
    }

    Weather *weather = ecs_singleton_get_mut(world, Weather);
    if (!weather || weather->greenhouse_gas < required) {
        return false;
    }
    weather->greenhouse_gas -= required;
    ecs_singleton_modified(world, Weather);
    return true;
}

static bool biome_factory_takeInputs(
    ecs_world_t *world,
    const BiomeFactory *factory,
    const BiomeRecipe *recipe,
    BiomeResourceStorage *storage)
{
    if (factory->input_mode == BiomeFactoryInputCapture) {
        return biome_factory_captureInputs(world, recipe);
    }
    return biome_factory_consumeInputs(recipe, storage);
}

static void biome_factory_requestInputs(
    ecs_world_t *world,
    ecs_entity_t factory,
    const BiomeRecipe *recipe,
    const BiomeResourceStorageDesc *desc,
    BiomeResourceStorage *storage)
{
    if (!ecs_map_is_init(&recipe->inputs)) {
        return;
    }

    bool modified = false;
    ecs_map_iter_t it = ecs_map_iter(&recipe->inputs);
    while (ecs_map_next(&it)) {
        ecs_entity_t resource = ecs_map_key(&it);
        int32_t stored = biome_factory_mapValue(
            &storage->resources, resource);
        int32_t reserved = biome_factory_mapValue(
            &storage->reserved, resource);
        int32_t room = desc->capacity - stored - reserved;

        const BiomeResource *resource_config = ecs_get(
            world, resource, BiomeResource);
        if (!resource_config ||
            resource_config->max_drone_amount <= 0 ||
            room <= 0)
        {
            continue;
        }

        int32_t amount = resource_config->max_drone_amount;
        if (amount > room) {
            amount = room;
        }

        int32_t *value = biome_factory_mapEnsure(
            &storage->reserved, resource);
        *value += amount;
        if (!biome_factory_combineRequest(
            world, factory, resource, amount,
            resource_config->max_drone_amount, storage))
        {
            biome_logistics_postRequest(world, BiomeRequestDropOff,
                factory, resource, amount, 0);
        }
        modified = true;
    }

    if (modified) {
        ecs_modified_id(world, factory, ecs_id(BiomeResourceStorage));
    }
}

static void biome_factory_postPickupRequests(
    ecs_world_t *world,
    ecs_entity_t factory,
    ecs_entity_t resource_entity,
    BiomeResourceStorage *storage)
{
    const BiomeResource *resource = ecs_get(
        world, resource_entity, BiomeResource);
    if (!resource || resource->min_drone_amount <= 0) {
        return;
    }

    int32_t stored = biome_factory_mapValue(
        &storage->resources, resource_entity);
    int32_t reserved = biome_factory_mapValue(
        &storage->reserved, resource_entity);
    while (stored - reserved >= resource->min_drone_amount) {
        int32_t *value = biome_factory_mapEnsure(
            &storage->reserved, resource_entity);
        *value += resource->min_drone_amount;
        reserved += resource->min_drone_amount;
        biome_logistics_postRequest(world, BiomeRequestPickup,
            factory, resource_entity, resource->min_drone_amount, 0);
    }
}

static bool biome_factory_outputRoom(
    const BiomeFactory *factory,
    const BiomeRecipe *recipe,
    const BiomeResourceStorageDesc *desc,
    const BiomeResourceStorage *storage)
{
    if (factory->output_mode != BiomeFactoryOutputStore ||
        !recipe->output)
    {
        return true;
    }
    return biome_factory_mapValue(
        &storage->resources, recipe->output) < desc->capacity;
}

static void biome_factory_store(
    ecs_world_t *world,
    ecs_entity_t entity,
    const BiomeFactory *factory,
    const BiomeRecipe *recipe,
    const BiomeResourceStorageDesc *desc,
    BiomeResourceStorage *storage)
{
    if (factory->output_mode != BiomeFactoryOutputStore ||
        !recipe->output)
    {
        return;
    }

    int32_t *stored = biome_factory_mapEnsure(
        &storage->resources, recipe->output);
    if (*stored >= desc->capacity) {
        return;
    }
    *stored += 1;
    biome_factory_postPickupRequests(
        world, entity, recipe->output, storage);
    ecs_modified_id(world, entity, ecs_id(BiomeResourceStorage));
}

static void biome_factory_ventWeather(
    ecs_world_t *world,
    ecs_entity_t resource_entity,
    int32_t amount)
{
    const BiomeResource *resource = ecs_get(
        world, resource_entity, BiomeResource);
    if (!resource ||
        (resource->greenhouse_gas == 0 && resource->o2 == 0))
    {
        return;
    }

    Weather *weather = ecs_singleton_get_mut(world, Weather);
    if (!weather) {
        return;
    }
    weather->greenhouse_gas += resource->greenhouse_gas * (float)amount;
    weather->o2 += resource->o2 * (float)amount;
    ecs_singleton_modified(world, Weather);
}

static void biome_factory_ventTiles(
    ecs_world_t *world,
    ecs_entity_t entity,
    ecs_entity_t resource_entity,
    int32_t amount)
{
    const BiomeResource *resource = ecs_get(
        world, resource_entity, BiomeResource);
    if (!resource ||
        (resource->fertility == 0 && resource->moisture == 0))
    {
        return;
    }

    const FlecsTerrainPosition *tp = ecs_get(
        world, entity, FlecsTerrainPosition);
    if (!tp || !tp->terrain || !ecs_is_alive(world, tp->terrain)) {
        return;
    }

    const FlecsTerrain *t = ecs_get(world, tp->terrain, FlecsTerrain);
    if (!t || ecs_vec_count(&t->layerTypes) <= TerrainGroundIndex) {
        return;
    }

    TerrainSoil *soil = flecsEngine_terrain_getLayer(
        world, tp->terrain, TerrainSoilIndex, TerrainSoil);
    TerrainGround *ground = flecsEngine_terrain_getLayer(
        world, tp->terrain, TerrainGroundIndex, TerrainGround);
    if (!soil || !ground) {
        return;
    }

    float fertility = resource->fertility * (float)amount;
    float moisture = resource->moisture * (float)amount;
    for (int32_t dy = -1; dy <= 1; dy ++) {
        for (int32_t dx = -1; dx <= 1; dx ++) {
            int32_t x = tp->x + dx;
            int32_t y = tp->y + dy;
            if (x < 0 || x >= t->width || y < 0 || y >= t->depth) {
                continue;
            }
            float scale = (dx || dy) ? 0.25f : 1.0f;
            int32_t cell = y * t->width + x;
            soil[cell].fertility += fertility * scale;
            if (soil[cell].fertility > 1.0f) {
                soil[cell].fertility = 1.0f;
            }
            ground[cell].moisture += moisture * scale;
            if (ground[cell].moisture > 1.0f) {
                ground[cell].moisture = 1.0f;
            }
        }
    }
}

static void biome_factory_vent(
    ecs_world_t *world,
    const BiomeFactory *factory,
    const BiomeRecipe *recipe)
{
    if (factory->output_mode != BiomeFactoryOutputVent) {
        return;
    }

    if (ecs_map_is_init(&recipe->outputs)) {
        ecs_map_iter_t it = ecs_map_iter(&recipe->outputs);
        while (ecs_map_next(&it)) {
            biome_factory_ventWeather(
                world, ecs_map_key(&it), (int32_t)ecs_map_value(&it));
        }
    } else if (recipe->output) {
        biome_factory_ventWeather(world, recipe->output, 1);
    }
}

static void biome_factory_ventGround(
    ecs_world_t *world,
    ecs_entity_t entity,
    const BiomeFactory *factory,
    const BiomeRecipe *recipe)
{
    if (factory->output_mode != BiomeFactoryOutputVent) {
        return;
    }

    if (ecs_map_is_init(&recipe->outputs)) {
        ecs_map_iter_t it = ecs_map_iter(&recipe->outputs);
        while (ecs_map_next(&it)) {
            biome_factory_ventTiles(
                world, entity, ecs_map_key(&it),
                (int32_t)ecs_map_value(&it));
        }
    } else if (recipe->output) {
        biome_factory_ventTiles(world, entity, recipe->output, 1);
    }
}

static void BiomeFactoryUpdate(ecs_iter_t *it) {
    BiomeFactoryProgress *progress = ecs_field(
        it, BiomeFactoryProgress, 0);
    BiomeResourceStorage *storages = ecs_field(
        it, BiomeResourceStorage, 1);

    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t entity = it->entities[i];
        const BiomeFactory *factory = ecs_get(
            it->world, entity, BiomeFactory);
        const BiomeResourceStorageDesc *desc = ecs_get(
            it->world, entity, BiomeResourceStorageDesc);
        if (!factory || !desc || !factory->recipe) {
            continue;
        }
        if (desc->kind != BiomeResourceStorageKindFactory &&
            (factory->output_mode != BiomeFactoryOutputVent ||
                desc->kind != BiomeResourceStorageKindSink))
        {
            continue;
        }

        const BiomeRecipe *recipe = ecs_get(
            it->world, factory->recipe, BiomeRecipe);
        if (!recipe) {
            continue;
        }

        BiomeFactoryProgress *p = &progress[i];
        BiomeResourceStorage *storage = &storages[i];
        if (p->remaining <= 0) {
            if (!biome_factory_outputRoom(
                factory, recipe, desc, storage))
            {
                continue;
            }
            if (!biome_factory_takeInputs(
                it->world, factory, recipe, storage))
            {
                if (factory->input_mode == BiomeFactoryInputSink) {
                    biome_factory_requestInputs(
                        it->world, entity, recipe, desc, storage);
                }
                continue;
            }
            p->remaining = recipe->craft_time;
            if (p->remaining < 1) {
                p->remaining = 1;
            }
            p->total = p->remaining;
            ecs_modified_id(
                it->world, entity, ecs_id(BiomeResourceStorage));
            if (factory->input_mode == BiomeFactoryInputSink) {
                biome_factory_requestInputs(
                    it->world, entity, recipe, desc, storage);
            }
        }

        const BiomePowerConsumer *power = ecs_get(
            it->world, entity, BiomePowerConsumer);
        if (power && !power->powered) {
            continue;
        }

        p->remaining -= 1;
        if (p->remaining > 0) {
            continue;
        }

        biome_factory_vent(it->world, factory, recipe);
        biome_factory_ventGround(it->world, entity, factory, recipe);
        biome_factory_store(
            it->world, entity, factory, recipe, desc, storage);
        if (biome_factory_outputRoom(factory, recipe, desc, storage) &&
            biome_factory_takeInputs(
                it->world, factory, recipe, storage))
        {
            p->remaining = recipe->craft_time;
            if (p->remaining < 1) {
                p->remaining = 1;
            }
            p->total = p->remaining;
            ecs_modified_id(
                it->world, entity, ecs_id(BiomeResourceStorage));
        }
        if (factory->input_mode == BiomeFactoryInputSink) {
            biome_factory_requestInputs(
                it->world, entity, recipe, desc, storage);
        }
    }
}

static void BiomeFactoryUpdateEmitter(ecs_iter_t *it) {
    const BiomeFactoryProgress *progress = ecs_field(
        it, BiomeFactoryProgress, 0);
    const BiomePowerConsumer *power = ecs_field(
        it, BiomePowerConsumer, 1);
    FlecsParticleEmitter *emitters = ecs_field(
        it, FlecsParticleEmitter, 2);

    for (int32_t i = 0; i < it->count; i ++) {
        emitters[i].enabled =
            power[i].powered && progress[i].remaining > 0;
    }
}

bool biome_factory_isActive(
    const ecs_world_t *world,
    ecs_entity_t entity)
{
    const BiomeFactoryProgress *progress = ecs_get(
        world, entity, BiomeFactoryProgress);
    return progress && progress->remaining > 0;
}

int32_t biome_factory_canAfford(
    const ecs_world_t *world,
    ecs_entity_t item)
{
    const BiomeRecipe *recipe = ecs_get(world, item, BiomeRecipe);
    if (!recipe || !ecs_map_is_init(&recipe->inputs)) {
        return INT32_MAX;
    }

    ecs_world_t *w = ECS_CONST_CAST(ecs_world_t*, world);
    ecs_value_t totals = biome_playerAttr_get(w, "ResourceTotals");
    if (!totals.ptr) {
        return 0;
    }

    int32_t result = INT32_MAX;
    ecs_map_iter_t rit = ecs_map_iter(&recipe->inputs);
    while (ecs_map_next(&rit)) {
        int32_t required = (int32_t)ecs_map_value(&rit);
        if (required <= 0) {
            continue;
        }

        int32_t have = 0;
        ecs_map_val_t *v = ecs_map_get(totals.ptr, ecs_map_key(&rit));
        if (v) {
            have = *(int32_t*)v;
        }

        int32_t affordable = have / required;
        if (affordable < result) {
            result = affordable;
        }
    }

    return result;
}

bool biome_factory_purchase(
    const ecs_world_t *world,
    ecs_entity_t item,
    int32_t count)
{
    if (count < 0) {
        return false;
    }
    if (!count) {
        return true;
    }

    const BiomeRecipe *recipe = ecs_get(world, item, BiomeRecipe);
    if (!recipe || !ecs_map_is_init(&recipe->inputs)) {
        return true;
    }
    ecs_world_t *w = ECS_CONST_CAST(ecs_world_t*, world);
    if (biome_factory_canAfford(world, item) < count) {
        return false;
    }

    ecs_map_iter_t it = ecs_map_iter(&recipe->inputs);
    while (ecs_map_next(&it)) {
        int32_t required = (int32_t)ecs_map_value(&it);
        if (required > 0) {
            biome_resource_playerAdd(
                w, "ResourceTotals", ecs_map_key(&it),
                -required * count);
        }
    }
    return true;
}

void biome_factory_refund(
    const ecs_world_t *world,
    ecs_entity_t item,
    int32_t count)
{
    if (count <= 0) {
        return;
    }

    const BiomeRecipe *recipe = ecs_get(world, item, BiomeRecipe);
    if (!recipe || !ecs_map_is_init(&recipe->inputs)) {
        return;
    }

    ecs_world_t *w = ECS_CONST_CAST(ecs_world_t*, world);
    ecs_map_iter_t it = ecs_map_iter(&recipe->inputs);
    while (ecs_map_next(&it)) {
        int32_t amount = (int32_t)ecs_map_value(&it);
        if (amount <= 0) {
            continue;
        }
        ecs_entity_t resource = ecs_map_key(&it);
        int32_t stored = biome_resource_playerAmount(
            w, "ResourceTotals", resource);
        int32_t capacity = biome_resource_playerAmount(
            w, "ResourceCapacityTotals", resource);
        int64_t refund = (int64_t)amount * count;
        int32_t room = capacity - stored;
        if (room > 0) {
            biome_resource_playerAdd(
                w, "ResourceTotals", resource,
                refund < room ? (int32_t)refund : room);
        }
    }
}

void biomeFactoryImport(ecs_world_t *world) {
    ECS_MODULE(world, biomeFactory);

    ECS_IMPORT(world, biomeResources);
    ECS_IMPORT(world, biomeLogistics);
    ECS_IMPORT(world, biomeWeather);
    ECS_IMPORT(world, biomePower);

    ecs_set_name_prefix(world, "Biome");

    ecs_id(biome_factory_outputMode_t) = ecs_enum(world, {
        .entity = ecs_entity(world, {
            .name = "OutputMode",
            .symbol = "biome_factory_outputMode_t"
        }),
        .constants = {
            {"Vent", BiomeFactoryOutputVent},
            {"Store", BiomeFactoryOutputStore}
        }
    });
    ecs_id(biome_factory_inputMode_t) = ecs_enum(world, {
        .entity = ecs_entity(world, {
            .name = "InputMode",
            .symbol = "biome_factory_inputMode_t"
        }),
        .constants = {
            {"Sink", BiomeFactoryInputSink},
            {"Capture", BiomeFactoryInputCapture}
        }
    });
    ECS_META_COMPONENT(world, BiomeFactory);
    ECS_COMPONENT_DEFINE(world, BiomeFactoryProgress);

    ecs_struct(world, {
        .entity = ecs_id(BiomeFactoryProgress),
        .members = {
            { .name = "remaining", .type = ecs_id(ecs_f32_t),
                .offset = offsetof(BiomeFactoryProgress, remaining) },
            { .name = "total", .type = ecs_id(ecs_f32_t),
                .offset = offsetof(BiomeFactoryProgress, total) }
        }
    });

    ecs_add_pair(world, ecs_id(BiomeFactory),
        EcsWith, ecs_id(BiomeResourceStorage));
    ecs_add_pair(world, ecs_id(BiomeFactory),
        EcsWith, ecs_id(BiomeFactoryProgress));
    ecs_add_pair(world, ecs_id(BiomeFactoryProgress),
        EcsOnInstantiate, EcsOverride);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "Update" }),
        .query.terms = {
            { .id = ecs_id(BiomeFactoryProgress) },
            { .id = ecs_id(BiomeResourceStorage) }
        },
        .phase = EcsOnUpdate,
        .callback = BiomeFactoryUpdate
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "UpdateEmitter" }),
        .query.terms = {
            { .id = ecs_id(BiomeFactoryProgress), .inout = EcsIn },
            { .id = ecs_id(BiomePowerConsumer), .inout = EcsIn },
            { .id = ecs_id(FlecsParticleEmitter) }
        },
        .phase = EcsOnUpdate,
        .callback = BiomeFactoryUpdateEmitter
    });
}
