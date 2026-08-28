#define BIOME_LOGISTICS_IMPL

#include "biome.h"
#include "../tasks/logistics.h"

int32_t biome_logistics_mapValue(
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

void biome_logistics_addMapValue(
    BiomeResourceStorageMap *map,
    ecs_entity_t resource,
    int32_t amount)
{
    if (!ecs_map_is_init(map)) {
        ecs_map_init(map, NULL);
    }
    int32_t *value = (int32_t*)ecs_map_ensure(
        map, (ecs_map_key_t)resource);
    *value += amount;
}

typedef struct BiomeLogisticsPlayerAttrs {
    const BiomeResourceStorageMap *totals;
    const BiomeResourceStorageMap *reserved;
    const BiomeResourceStorageMap *capacity;
} BiomeLogisticsPlayerAttrs;

static BiomeLogisticsPlayerAttrs biome_logistics_playerAttrs(
    ecs_world_t *world)
{
    return (BiomeLogisticsPlayerAttrs) {
        biome_playerAttr_get(world, "ResourceTotals").ptr,
        biome_playerAttr_get(world, "ResourceReservedTotals").ptr,
        biome_playerAttr_get(world, "ResourceCapacityTotals").ptr
    };
}

static bool biome_logistics_entityPowered(
    const ecs_world_t *world,
    ecs_entity_t storage)
{
    const BiomePowerConsumer *power = ecs_get(
        world, storage, BiomePowerConsumer);
    return !power || power->powered;
}

static bool biome_logistics_playerFits(
    ecs_world_t *world,
    const BiomeLogisticsPlayerAttrs *attrs,
    ecs_entity_t resource,
    int32_t amount,
    bool pickup,
    bool reserve)
{
    int32_t stored = biome_logistics_mapValue(
        attrs->totals, resource);
    int32_t reserved = biome_logistics_mapValue(
        attrs->reserved, resource);
    bool fits;
    if (pickup) {
        fits = stored - reserved >= amount;
    } else {
        int32_t capacity = biome_logistics_mapValue(
            attrs->capacity, resource);
        fits = capacity - stored - reserved >= amount;
    }

    return fits && (!reserve || biome_resource_playerAdd(
        world, "ResourceReservedTotals", resource, amount));
}

static bool biome_logistics_getRequest(
    const ecs_world_t *world,
    ecs_entity_t request_entity,
    BiomeLogisticsRequest *result)
{
    if (!ecs_is_alive(world, request_entity)) {
        return false;
    }

    ecs_entity_t request_kind = ecs_get_target(
        world, request_entity, ecs_id(BiomeLogisticsRequest), 0);
    const BiomeLogisticsRequest *request = request_kind
        ? ecs_get_id(world, request_entity, ecs_pair(
            ecs_id(BiomeLogisticsRequest), request_kind))
        : NULL;
    if (!request ||
        (request->kind != BiomeRequestPickup &&
            request->kind != BiomeRequestDropOff) ||
        request->amount <= 0 ||
        !ecs_is_alive(world, request->source))
    {
        return false;
    }

    *result = *request;
    return true;
}

static bool biome_logistics_requestEndpointFits(
    const ecs_world_t *world,
    const BiomeLogisticsRequest *request,
    int32_t amount)
{
    const BiomeResourceStorage *storage = ecs_get(
        world, request->source, BiomeResourceStorage);
    const BiomeResourceStorageDesc *desc = ecs_get(
        world, request->source, BiomeResourceStorageDesc);
    if (!storage || !desc) {
        return false;
    }

    int32_t stored = biome_logistics_mapValue(
        &storage->resources, request->resource);
    if (request->kind == BiomeRequestPickup) {
        return stored >= amount;
    }
    if (request->kind == BiomeRequestDropOff) {
        return desc->capacity - stored >= amount;
    }
    return false;
}

static bool biome_logistics_resolveRequestWithAttrs(
    ecs_world_t *world,
    const BiomeLogisticsPlayerAttrs *attrs,
    const BiomeLogisticsRequest *request,
    int32_t amount,
    ecs_entity_t storage,
    bool reserve,
    BiomeLogisticsJob *job)
{
    if (amount <= 0 || !storage ||
        !ecs_has(world, storage, BiomePlayerStorage) ||
        !biome_logistics_requestEndpointFits(world, request, amount))
    {
        return false;
    }

    ecs_entity_t src;
    ecs_entity_t dst;
    if (request->kind == BiomeRequestPickup) {
        src = request->source;
        dst = storage;
        if (!biome_logistics_playerFits(
            world, attrs, request->resource, amount, false, reserve))
        {
            return false;
        }
    } else if (request->kind == BiomeRequestDropOff) {
        dst = request->source;
        src = storage;
        if (!biome_logistics_playerFits(
            world, attrs, request->resource, amount, true, reserve))
        {
            return false;
        }
    } else {
        return false;
    }
    if (!src || !dst) {
        return false;
    }

    *job = (BiomeLogisticsJob){
        request->resource,
        amount,
        src,
        dst
    };
    return true;
}

static bool biome_logistics_resolveRequest(
    ecs_world_t *world,
    const BiomeLogisticsRequest *request,
    int32_t amount,
    ecs_entity_t storage,
    bool reserve,
    BiomeLogisticsJob *job)
{
    BiomeLogisticsPlayerAttrs attrs =
        biome_logistics_playerAttrs(world);
    return biome_logistics_resolveRequestWithAttrs(
        world, &attrs, request, amount, storage, reserve, job);
}

static bool biome_logistics_tryRequest(
    ecs_world_t *world,
    ecs_entity_t request_entity,
    ecs_entity_t storage,
    bool reserve,
    BiomeLogisticsJob *job)
{
    BiomeLogisticsRequest request;
    return biome_logistics_getRequest(
        world, request_entity, &request) &&
        biome_logistics_resolveRequest(
            world, &request, request.amount, storage, reserve, job);
}

static bool biome_logistics_acceptRequestWithAttrs(
    ecs_world_t *world,
    const BiomeLogisticsPlayerAttrs *attrs,
    ecs_entity_t request_entity,
    ecs_entity_t storage_entity,
    ecs_vec_t *accepted,
    BiomeLogisticsJob *job)
{
    ecs_vec_clear(accepted);

    BiomeLogisticsRequest request;
    if (!biome_logistics_getRequest(
        world, request_entity, &request))
    {
        return false;
    }

    int32_t max_amount = request.amount;
    const BiomeResource *resource = ecs_get(
        world, request.resource, BiomeResource);
    if (resource && resource->max_drone_amount > max_amount) {
        max_amount = resource->max_drone_amount;
    }

    int32_t amount = request.amount;
    *ecs_vec_append_t(NULL, accepted, ecs_entity_t) = request_entity;

    const BiomeResourceStorage *storage = ecs_get(
        world, request.source, BiomeResourceStorage);
    if (storage) {
        int32_t count = ecs_vec_count(&storage->outstanding_requests);
        const ecs_entity_t *requests = ecs_vec_first_t(
            &storage->outstanding_requests, ecs_entity_t);
        bool found = false;

        for (int32_t i = 0; i < count; i ++) {
            ecs_entity_t candidate_entity = requests[i];
            if (candidate_entity == request_entity) {
                found = true;
                continue;
            }
            if (!found) {
                continue;
            }

            BiomeLogisticsRequest candidate;
            if (!biome_logistics_getRequest(
                    world, candidate_entity, &candidate) ||
                candidate.source != request.source ||
                candidate.kind != request.kind ||
                candidate.resource != request.resource ||
                candidate.amount > max_amount - amount)
            {
                continue;
            }

            BiomeLogisticsJob combined_job;
            if (!biome_logistics_resolveRequestWithAttrs(
                world, attrs, &request, amount + candidate.amount,
                storage_entity, false, &combined_job))
            {
                continue;
            }

            amount += candidate.amount;
            *ecs_vec_append_t(NULL, accepted, ecs_entity_t) =
                candidate_entity;
            if (amount == max_amount) {
                break;
            }
        }
    }

    if (!biome_logistics_resolveRequestWithAttrs(
        world, attrs, &request, amount, storage_entity, true, job))
    {
        ecs_vec_clear(accepted);
        return false;
    }

    return true;
}

static bool biome_logistics_acceptRequest(
    ecs_world_t *world,
    ecs_entity_t request_entity,
    ecs_entity_t storage_entity,
    ecs_vec_t *accepted,
    BiomeLogisticsJob *job)
{
    BiomeLogisticsPlayerAttrs attrs =
        biome_logistics_playerAttrs(world);
    return biome_logistics_acceptRequestWithAttrs(
        world, &attrs, request_entity, storage_entity, accepted, job);
}

static void biome_logistics_collectRequests(
    ecs_world_t *world,
    ecs_entity_t storage,
    ecs_vec_t *requests)
{
    ecs_entity_t jobs = ecs_lookup(world, "jobs");
    ecs_assert(jobs != 0, ECS_INTERNAL_ERROR, NULL);

    ecs_iter_t it = ecs_children(world, jobs);
    while (ecs_children_next(&it)) {
        for (int32_t i = 0; i < it.count; i ++) {
            BiomeLogisticsJob job;
            if (biome_logistics_tryRequest(
                world, it.entities[i], storage, false, &job))
            {
                *ecs_vec_append_t(NULL, requests, ecs_entity_t) =
                    it.entities[i];
            }
        }
    }
}

static void biome_logistics_collectPendingRequests(
    ecs_world_t *world,
    ecs_vec_t *requests)
{
    ecs_entity_t jobs = ecs_lookup(world, "jobs");
    ecs_assert(jobs != 0, ECS_INTERNAL_ERROR, NULL);

    ecs_iter_t it = ecs_children(world, jobs);
    while (ecs_children_next(&it)) {
        for (int32_t i = 0; i < it.count; i ++) {
            *ecs_vec_append_t(NULL, requests, ecs_entity_t) =
                it.entities[i];
        }
    }
}

static void biome_logistics_removeOutstandingRequest(
    ecs_world_t *world,
    ecs_entity_t source,
    ecs_entity_t request)
{
    BiomeResourceStorage *storage = ecs_get_mut(
        world, source, BiomeResourceStorage);
    if (!storage) {
        return;
    }

    int32_t count = ecs_vec_count(&storage->outstanding_requests);
    ecs_entity_t *requests = ecs_vec_first_t(
        &storage->outstanding_requests, ecs_entity_t);
    for (int32_t i = 0; i < count; i ++) {
        if (requests[i] == request) {
            ecs_vec_remove_ordered_t(
                &storage->outstanding_requests, ecs_entity_t, i);
            ecs_modified_id(
                world, source, ecs_id(BiomeResourceStorage));
            return;
        }
    }
}

static void biome_logistics_finishRequests(
    ecs_world_t *world,
    ecs_vec_t *accepted)
{
    int32_t accepted_count = ecs_vec_count(accepted);
    ecs_entity_t *accepted_requests = ecs_vec_first_t(
        accepted, ecs_entity_t);
    for (int32_t r = 0; r < accepted_count; r ++) {
        BiomeLogisticsRequest accepted_request;
        if (biome_logistics_getRequest(
            world, accepted_requests[r], &accepted_request))
        {
            biome_logistics_removeOutstandingRequest(
                world, accepted_request.source, accepted_requests[r]);
        }
        ecs_delete(world, accepted_requests[r]);
    }
}

static void biome_logistics_dispatch(
    ecs_iter_t *waiter_it)
{
    ecs_world_t *world = waiter_it->world;
    ecs_vec_t accepted;
    ecs_vec_init_t(NULL, &accepted, ecs_entity_t, 0);

    BiomeLogisticsPlayerAttrs attrs =
        biome_logistics_playerAttrs(world);

    ecs_vec_t requests;
    ecs_vec_init_t(NULL, &requests, ecs_entity_t, 0);
    biome_logistics_collectPendingRequests(world, &requests);

    int32_t request_index = 0;
    int32_t request_count = ecs_vec_count(&requests);
    ecs_entity_t *request_entities = ecs_vec_first_t(
        &requests, ecs_entity_t);

    while (ecs_query_next(waiter_it)) {
        BiomeLogisticsWaiter *waiters = ecs_field(
            waiter_it, BiomeLogisticsWaiter, 0);
        const BiomeLogisticsCarrier *carriers = ecs_field(
            waiter_it, BiomeLogisticsCarrier, 1);

        for (int32_t i = 0; i < waiter_it->count; i ++)
        {
            const BiomeLogisticsCarrier *carrier = &carriers[i];
            if (!carrier || !carrier->storage ||
                (carrier->home &&
                    !biome_logistics_entityPowered(
                        world, carrier->home)))
            {
                continue;
            }

            BiomeLogisticsJob job;
            ecs_entity_t request = 0;
            while (request_index < request_count) {
                ecs_entity_t candidate =
                    request_entities[request_index ++];
                if (biome_logistics_acceptRequestWithAttrs(
                    world, &attrs, candidate, carrier->storage,
                    &accepted, &job))
                {
                    request = candidate;
                    break;
                }
            }
            if (!request) {
                continue;
            }

            ecs_value_t value = {
                ecs_id(BiomeLogisticsJob), &job
            };
            ecs_script_future_resolve(waiters[i].future, &value);
            ecs_remove(
                world, waiter_it->entities[i], BiomeLogisticsWaiter);

            ecs_defer_suspend(world);
            biome_logistics_finishRequests(world, &accepted);
            ecs_defer_resume(world);
        }
    }

    ecs_vec_fini_t(NULL, &accepted, ecs_entity_t);
    ecs_vec_fini_t(NULL, &requests, ecs_entity_t);
}

static void BiomeLogisticsDispatch(ecs_iter_t *it) {
    biome_logistics_dispatch(it);
}

void biome_logistics_postRequest(
    ecs_world_t *world,
    biome_logisticsRequestKind_t kind,
    ecs_entity_t source,
    ecs_entity_t resource,
    int32_t amount,
    int32_t priority)
{
    if (amount <= 0) {
        return;
    }

    ecs_entity_t jobs = ecs_lookup(world, "jobs");
    ecs_assert(jobs != 0, ECS_INTERNAL_ERROR, NULL);

    const EcsConstants *request_kinds = ecs_get(world,
        ecs_id(biome_logisticsRequestKind_t), EcsConstants);
    ecs_assert(request_kinds != NULL, ECS_INTERNAL_ERROR, NULL);

    const ecs_enum_constant_t *request_kind = ecs_map_get_deref(
        request_kinds->constants, ecs_enum_constant_t, kind);
    ecs_assert(request_kind != NULL, ECS_INVALID_PARAMETER, NULL);

    int32_t max_amount = amount;
    const BiomeResource *resource_config = ecs_get(
        world, resource, BiomeResource);
    if (resource_config && resource_config->max_drone_amount > 0) {
        max_amount = resource_config->max_drone_amount;
    }

    while (amount > 0) {
        int32_t request_amount = amount < max_amount ? amount : max_amount;
        ecs_entity_t job = ecs_new_w_pair(world, EcsChildOf, jobs);
        ecs_set_pair(
            world, job, BiomeLogisticsRequest, request_kind->constant, {
                kind, source, resource, request_amount, priority
            });
        BiomeResourceStorage *storage = ecs_get_mut(
            world, source, BiomeResourceStorage);
        if (storage) {
            ecs_vec_init_if_t(
                &storage->outstanding_requests, ecs_entity_t);
            *ecs_vec_append_t(
                NULL, &storage->outstanding_requests, ecs_entity_t) = job;
            ecs_modified_id(
                world, source, ecs_id(BiomeResourceStorage));
        }

        amount -= request_amount;
    }
}

static void biome_logistics_storageCells(
    ecs_world_t *world,
    ecs_entity_t terrain,
    int32_t terrain_width,
    int32_t terrain_depth,
    ecs_entity_t *cells)
{
    ecs_iter_t it = ecs_each(world, BiomePlayerStorage);
    while (ecs_each_next(&it)) {
        for (int32_t i = 0; i < it.count; i ++) {
            const FlecsTerrainPosition *position = ecs_get(
                world, it.entities[i], FlecsTerrainPosition);
            if (!position || position->terrain != terrain) {
                continue;
            }

            int32_t width = position->span_x ? position->span_x : 1;
            int32_t height = position->span_y ? position->span_y : 1;
            for (int32_t y = position->y;
                y < position->y + height;
                y ++)
            {
                if (y < 0 || y >= terrain_depth) {
                    continue;
                }
                for (int32_t x = position->x;
                    x < position->x + width;
                    x ++)
                {
                    if (x >= 0 && x < terrain_width) {
                        cells[y * terrain_width + x] = it.entities[i];
                    }
                }
            }
        }
    }
}

static ecs_entity_t biome_logistics_findClosestStorage(
    ecs_world_t *world,
    ecs_entity_t home)
{
    const FlecsTerrainPosition *position = ecs_get(
        world, home, FlecsTerrainPosition);
    if (!position || !position->terrain) {
        return 0;
    }

    const FlecsTerrain *terrain = ecs_get(
        world, position->terrain, FlecsTerrain);
    if (!terrain || terrain->width <= 0 || terrain->depth <= 0) {
        return 0;
    }

    int32_t cell_count = terrain->width * terrain->depth;
    bool *visited = ecs_os_calloc_n(bool, cell_count);
    ecs_entity_t *storage_cells = ecs_os_calloc_n(
        ecs_entity_t, cell_count);
    biome_logistics_storageCells(
        world, position->terrain, terrain->width, terrain->depth,
        storage_cells);
    ecs_vec_t queue;
    ecs_vec_init_t(NULL, &queue, int32_t, 0);

    int32_t width = position->span_x ? position->span_x : 1;
    int32_t height = position->span_y ? position->span_y : 1;
    for (int32_t y = position->y; y < position->y + height; y ++) {
        for (int32_t x = position->x; x < position->x + width; x ++) {
            if (x < 0 || y < 0 ||
                x >= terrain->width || y >= terrain->depth)
            {
                continue;
            }
            int32_t index = y * terrain->width + x;
            if (!visited[index]) {
                visited[index] = true;
                *ecs_vec_append_t(NULL, &queue, int32_t) = index;
            }
        }
    }

    static const int32_t dx[] = {0, 1, 0, -1};
    static const int32_t dy[] = {-1, 0, 1, 0};
    ecs_entity_t result = 0;
    int32_t head = 0;
    while (head < ecs_vec_count(&queue)) {
        int32_t index = ecs_vec_get_t(&queue, int32_t, head)[0];
        head ++;
        int32_t x = index % terrain->width;
        int32_t y = index / terrain->width;

        result = storage_cells[index];
        if (result) {
            break;
        }

        for (int32_t direction = 0; direction < 4; direction ++) {
            int32_t next_x = x + dx[direction];
            int32_t next_y = y + dy[direction];
            if (next_x < 0 || next_y < 0 ||
                next_x >= terrain->width || next_y >= terrain->depth)
            {
                continue;
            }
            int32_t next = next_y * terrain->width + next_x;
            if (!visited[next]) {
                visited[next] = true;
                *ecs_vec_append_t(NULL, &queue, int32_t) = next;
            }
        }
    }

    ecs_vec_fini_t(NULL, &queue, int32_t);
    ecs_os_free(storage_cells);
    ecs_os_free(visited);
    return result;
}

static void BiomeLogisticsCarrierOnSet(ecs_iter_t *it) {
    BiomeLogisticsCarrier *carriers = ecs_field(
        it, BiomeLogisticsCarrier, 0);
    for (int32_t i = 0; i < it->count; i ++) {
        carriers[i].storage = biome_logistics_findClosestStorage(
            it->world, carriers[i].home);
    }
}

void biomeLogisticsImport(ecs_world_t *world) {
    ECS_MODULE(world, biomeLogistics);

    ECS_IMPORT(world, biomeBehavior);
    ECS_IMPORT(world, biomeResources);
    ECS_IMPORT(world, biomeBuildings);
    ECS_IMPORT(world, biomePower);

    ecs_set_name_prefix(world, "BiomeLogistics");

    ECS_META_COMPONENT(world, biome_logisticsRequestKind_t);
    ECS_META_COMPONENT(world, BiomeLogisticsRequest);
    ECS_META_COMPONENT(world, BiomeLogisticsCarrier);
    ECS_META_COMPONENT(world, BiomeLogisticsJob);

    ecs_set_hooks(world, BiomeLogisticsCarrier, {
        .on_set = BiomeLogisticsCarrierOnSet
    });

    ecs_entity_t module = ecs_id(biomeLogistics);
    biomeLogisticsTasksImport(world, module);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "Dispatch" }),
        .query.terms = {
            { .id = ecs_id(BiomeLogisticsWaiter) },
            { .id = ecs_id(BiomeLogisticsCarrier), .inout = EcsIn }
        },
        .query.cache_kind = EcsQueryCacheAuto,
        .phase = EcsOnUpdate,
        .run = BiomeLogisticsDispatch,
        .immediate = true
    });

    ecs_entity_t prev_scope = ecs_set_scope(world, 0);
    ecs_entity_t jobs = ecs_entity(world, { .name = "jobs" });
    ecs_add_id(world, jobs, EcsOrderedChildren);
    ecs_set_scope(world, prev_scope);
}
