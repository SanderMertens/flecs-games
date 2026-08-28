#include "logistics.h"

static ecs_entity_t biome_logistics_destinationGone;

static void biome_logistics_rejectGone(
    ecs_script_future_t *future,
    const char *error)
{
    ecs_script_future_reject_id(
        future, biome_logistics_destinationGone, error);
}

static void biome_logistics_resolve(
    ecs_script_future_t *future)
{
    int32_t result = 0;
    ecs_script_future_resolve(future,
        &(ecs_value_t){ecs_id(ecs_i32_t), &result});
}

static void biome_logistics_acceptJob(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    (void)argc;
    (void)argv;
    ecs_set(ctx->world, ctx->entity, BiomeLogisticsWaiter, {future});
}

static void biome_logistics_cancelWaiter(
    const ecs_function_ctx_t *ctx,
    ecs_script_future_t *future)
{
    const BiomeLogisticsWaiter *waiter = ecs_get(
        ctx->world, ctx->entity, BiomeLogisticsWaiter);
    if (waiter && waiter->future == future) {
        ecs_remove(ctx->world, ctx->entity, BiomeLogisticsWaiter);
    }
}

static void biome_logistics_startMotion(
    const ecs_function_ctx_t *ctx,
    ecs_script_future_t *future,
    const FlecsPosition3 *target,
    ecs_entity_t terrain,
    float speed)
{
    if (speed <= 0) {
        ecs_script_future_reject(
            future, "motion speed must be greater than zero");
        return;
    }
    const FlecsPosition3 *position = ecs_get(
        ctx->world, ctx->entity, FlecsPosition3);
    if (!position) {
        biome_logistics_resolve(future);
        return;
    }
    float dx = target->x - position->x;
    float dy = target->y - position->y;
    float dz = target->z - position->z;
    float distance = sqrtf(dx * dx + dy * dy + dz * dz);
    int32_t duration = (int32_t)ceilf(distance / speed);
    if (!duration) {
        ecs_set_ptr(ctx->world, ctx->entity, FlecsPosition3, target);
        biome_logistics_resolve(future);
        return;
    }
    float start_clearance = 0;
    float target_clearance = 0;
    if (terrain) {
        const FlecsTerrain *t = ecs_get(ctx->world, terrain, FlecsTerrain);
        const FlecsPosition3 *terrain_position = ecs_get(
            ctx->world, terrain, FlecsPosition3);
        if (t) {
            float terrain_x = terrain_position ? terrain_position->x : 0;
            float terrain_y = terrain_position ? terrain_position->y : 0;
            float terrain_z = terrain_position ? terrain_position->z : 0;
            start_clearance = position->y - terrain_y -
                flecsEngine_terrainSampleHeight(
                    t, position->x - terrain_x, position->z - terrain_z);
            target_clearance = target->y - terrain_y -
                flecsEngine_terrainSampleHeight(
                    t, target->x - terrain_x, target->z - terrain_z);
        } else {
            terrain = 0;
        }
    }
    ecs_set(ctx->world, ctx->entity, BiomeLogisticsMotion, {
        future, *position, *target, terrain,
        start_clearance, target_clearance, 0, duration
    });
}

static void biome_logistics_takeOff(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    if (argc != 1) {
        ecs_script_future_reject(future, "takeOff expects a speed");
        return;
    }
    const FlecsPosition3 *position = ecs_get(
        ctx->world, ctx->entity, FlecsPosition3);
    if (!position) {
        biome_logistics_resolve(future);
        return;
    }
    FlecsPosition3 target = *position;
    target.y += 5.0f;
    biome_logistics_startMotion(
        ctx, future, &target, 0, *(float*)argv[0].ptr);
}

static void biome_logistics_land(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    if (argc != 2) {
        ecs_script_future_reject(
            future, "land expects a destination and speed");
        return;
    }
    ecs_entity_t dst = *(ecs_entity_t*)argv[0].ptr;
    if (!ecs_is_alive(ctx->world, dst)) {
        biome_logistics_rejectGone(future, "land destination no longer exists");
        return;
    }
    const FlecsPosition3 *position = ecs_get(
        ctx->world, ctx->entity, FlecsPosition3);
    const FlecsPosition3 *dst_position = ecs_get(
        ctx->world, dst, FlecsPosition3);
    FlecsAABB dst_aabb;
    FlecsAABB drone_aabb;
    if (!position || !dst_position ||
        !flecsEngine_objectWorldAABB(ctx->world, dst, &dst_aabb) ||
        !flecsEngine_objectWorldAABB(
            ctx->world, ctx->entity, &drone_aabb))
    {
        ecs_script_future_reject(
            future, "land destination or drone has no renderable bounds");
        return;
    }
    float drone_bottom_offset = position->y - drone_aabb.min[1];
    FlecsPosition3 target = {
        dst_position->x,
        dst_aabb.max[1] + drone_bottom_offset,
        dst_position->z
    };
    biome_logistics_startMotion(
        ctx, future, &target, 0, *(float*)argv[1].ptr);
}

static void biome_logistics_moveTo(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    if (argc != 2) {
        ecs_script_future_reject(
            future, "moveTo expects a destination and speed");
        return;
    }
    ecs_entity_t dst = *(ecs_entity_t*)argv[0].ptr;
    if (!ecs_is_alive(ctx->world, dst)) {
        biome_logistics_rejectGone(
            future, "moveTo destination no longer exists");
        return;
    }
    const FlecsPosition3 *position = ecs_get(
        ctx->world, ctx->entity, FlecsPosition3);
    const FlecsPosition3 *dst_position = ecs_get(
        ctx->world, dst, FlecsPosition3);
    if (!position || !dst_position) {
        ecs_script_future_reject(future, "moveTo destination has no position");
        return;
    }
    FlecsPosition3 target = {
        dst_position->x, dst_position->y + 6.0f, dst_position->z
    };
    const FlecsTerrainPosition *terrain_position = ecs_get(
        ctx->world, dst, FlecsTerrainPosition);
    biome_logistics_startMotion(
        ctx, future, &target,
        terrain_position ? terrain_position->terrain : 0,
        *(float*)argv[1].ptr);
}

static void biome_logistics_cancelMotion(
    const ecs_function_ctx_t *ctx,
    ecs_script_future_t *future)
{
    const BiomeLogisticsMotion *motion = ecs_get(
        ctx->world, ctx->entity, BiomeLogisticsMotion);
    if (motion && motion->future == future) {
        ecs_remove(ctx->world, ctx->entity, BiomeLogisticsMotion);
    }
}

static void BiomeLogisticsMove(ecs_iter_t *it) {
    BiomeLogisticsMotion *motions = ecs_field(
        it, BiomeLogisticsMotion, 0);
    FlecsPosition3 *positions = ecs_field(it, FlecsPosition3, 1);

    for (int32_t i = 0; i < it->count; i ++) {
        BiomeLogisticsMotion *motion = &motions[i];
        motion->elapsed ++;
        float t = (float)motion->elapsed / (float)motion->duration;
        if (t > 1.0f) {
            t = 1.0f;
        }
        FlecsPosition3 position = {
            motion->start.x + (motion->target.x - motion->start.x) * t,
            motion->start.y + (motion->target.y - motion->start.y) * t,
            motion->start.z + (motion->target.z - motion->start.z) * t
        };
        if (motion->terrain) {
            const FlecsTerrain *terrain = ecs_get(
                it->world, motion->terrain, FlecsTerrain);
            const FlecsPosition3 *terrain_position = ecs_get(
                it->world, motion->terrain, FlecsPosition3);
            if (terrain) {
                float terrain_x = terrain_position
                    ? terrain_position->x : 0;
                float terrain_y = terrain_position
                    ? terrain_position->y : 0;
                float terrain_z = terrain_position
                    ? terrain_position->z : 0;
                float clearance = motion->start_clearance +
                    (motion->target_clearance -
                        motion->start_clearance) * t;
                position.y = terrain_y +
                    flecsEngine_terrainSampleHeight(
                        terrain, position.x - terrain_x,
                        position.z - terrain_z) +
                    clearance;
            }
        }
        positions[i] = position;
        if (motion->elapsed >= motion->duration) {
            ecs_script_future_t *future = motion->future;
            ecs_remove(it->world, it->entities[i], BiomeLogisticsMotion);
            biome_logistics_resolve(future);
        }
    }
}

static bool biome_logistics_transfer(
    ecs_world_t *world,
    ecs_entity_t resource,
    int32_t amount,
    ecs_entity_t entity,
    bool pickup)
{
    if (amount <= 0) {
        return false;
    }
    if (ecs_has(world, entity, BiomePlayerStorage)) {
        int32_t stored = biome_resource_playerAmount(
            world, "ResourceTotals", resource);
        if (pickup && stored < amount) {
            return false;
        }
        if (!pickup) {
            int32_t capacity = biome_resource_playerAmount(
                world, "ResourceCapacityTotals", resource);
            if (capacity - stored < amount) {
                return false;
            }
        }
        if (!biome_resource_playerAdd(
            world, "ResourceTotals", resource, pickup ? -amount : amount))
        {
            return false;
        }
        int32_t reserved = biome_resource_playerAmount(
            world, "ResourceReservedTotals", resource);
        if (reserved >= amount) {
            biome_resource_playerAdd(
                world, "ResourceReservedTotals", resource, -amount);
        }
        return true;
    }

    BiomeResourceStorage *storage = ecs_get_mut(
        world, entity, BiomeResourceStorage);
    if (!storage) {
        return false;
    }
    int32_t stored = biome_logistics_mapValue(
        &storage->resources, resource);
    int32_t reserved = biome_logistics_mapValue(
        &storage->reserved, resource);
    if (pickup && stored < amount) {
        return false;
    }
    biome_logistics_addMapValue(
        &storage->resources, resource, pickup ? -amount : amount);
    if (reserved >= amount) {
        biome_logistics_addMapValue(
            &storage->reserved, resource, -amount);
    }
    ecs_modified_id(world, entity, ecs_id(BiomeResourceStorage));
    return true;
}

static void biome_logistics_pickUp(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    if (argc != 4) {
        ecs_script_future_reject(future, "pickup failed");
        return;
    }
    ecs_entity_t src = *(ecs_entity_t*)argv[2].ptr;
    if (!ecs_is_alive(ctx->world, src)) {
        biome_logistics_rejectGone(future, "pickup source no longer exists");
        return;
    }
    if (!biome_logistics_transfer(ctx->world,
        *(ecs_entity_t*)argv[0].ptr, *(int32_t*)argv[1].ptr, src, true))
    {
        ecs_script_future_reject(future, "pickup failed");
        return;
    }
    biome_logistics_resolve(future);
}

static void biome_logistics_dropOff(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    if (argc != 4) {
        ecs_script_future_reject(future, "dropoff failed");
        return;
    }
    ecs_entity_t dst = *(ecs_entity_t*)argv[3].ptr;
    if (!ecs_is_alive(ctx->world, dst)) {
        biome_logistics_rejectGone(
            future, "dropoff destination no longer exists");
        return;
    }
    if (!biome_logistics_transfer(ctx->world,
        *(ecs_entity_t*)argv[0].ptr, *(int32_t*)argv[1].ptr, dst, false))
    {
        ecs_script_future_reject(future, "dropoff failed");
        return;
    }
    biome_logistics_resolve(future);
}

void biomeLogisticsTasksImport(
    ecs_world_t *world,
    ecs_entity_t module)
{
    ECS_COMPONENT_DEFINE(world, BiomeLogisticsWaiter);
    ECS_COMPONENT_DEFINE(world, BiomeLogisticsMotion);

    biome_logistics_destinationGone = ecs_entity(world, {
        .name = "DestinationGone",
        .parent = module
    });

    ecs_async_function(world, {
        .name = "acceptJob",
        .parent = module,
        .return_type = ecs_id(BiomeLogisticsJob),
        .callback = biome_logistics_acceptJob,
        .cancel = biome_logistics_cancelWaiter
    });
    ecs_async_function(world, {
        .name = "takeOff",
        .parent = module,
        .return_type = ecs_id(ecs_i32_t),
        .params = {{"speed", ecs_id(ecs_f32_t)}},
        .callback = biome_logistics_takeOff,
        .cancel = biome_logistics_cancelMotion
    });
    ecs_async_function(world, {
        .name = "moveTo",
        .parent = module,
        .return_type = ecs_id(ecs_i32_t),
        .params = {
            {"dst", ecs_id(ecs_entity_t)},
            {"speed", ecs_id(ecs_f32_t)}
        },
        .callback = biome_logistics_moveTo,
        .cancel = biome_logistics_cancelMotion
    });
    ecs_async_function(world, {
        .name = "land",
        .parent = module,
        .return_type = ecs_id(ecs_i32_t),
        .params = {
            {"dst", ecs_id(ecs_entity_t)},
            {"speed", ecs_id(ecs_f32_t)}
        },
        .callback = biome_logistics_land,
        .cancel = biome_logistics_cancelMotion
    });
    ecs_async_function(world, {
        .name = "pickUp",
        .parent = module,
        .return_type = ecs_id(ecs_i32_t),
        .params = {
            {"resource", ecs_id(ecs_entity_t)},
            {"amount", ecs_id(ecs_i32_t)},
            {"src", ecs_id(ecs_entity_t)},
            {"drone", ecs_id(ecs_entity_t)}
        },
        .callback = biome_logistics_pickUp
    });
    ecs_async_function(world, {
        .name = "dropOff",
        .parent = module,
        .return_type = ecs_id(ecs_i32_t),
        .params = {
            {"resource", ecs_id(ecs_entity_t)},
            {"amount", ecs_id(ecs_i32_t)},
            {"drone", ecs_id(ecs_entity_t)},
            {"dst", ecs_id(ecs_entity_t)}
        },
        .callback = biome_logistics_dropOff
    });

    /* Position is matched rather than set: carriers are dynamic transforms,
     * so nothing observes the component and their world transform is
     * recomputed from it every frame anyway. Writing the field avoids a
     * command per carrier per frame. Self only, so the field is always
     * writable storage and never a shared prefab value. */
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "Move" }),
        .query.terms = {
            { .id = ecs_id(BiomeLogisticsMotion) },
            { .id = ecs_id(FlecsPosition3), .src.id = EcsSelf }
        },
        .phase = EcsOnUpdate,
        .callback = BiomeLogisticsMove
    });
}
