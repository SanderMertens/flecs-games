#define BIOME_BEHAVIOR_IMPL

#include "biome.h"

ECS_COMPONENT_DECLARE(BiomeBehaviors);

typedef struct BiomeBehaviorRuntime {
    ecs_script_task_t **tasks;
    int32_t count;
} BiomeBehaviorRuntime;

typedef struct BiomeBehaviorAliveWaiter {
    ecs_entity_t target;
    ecs_script_future_t *future;
} BiomeBehaviorAliveWaiter;

typedef struct BiomeBehaviorContext {
    ecs_vec_t alive_waiters;
} BiomeBehaviorContext;

ECS_COMPONENT_DECLARE(BiomeBehaviorRuntime);

static void biome_behavior_alive_waiter_fini(
    BiomeBehaviorAliveWaiter *waiter)
{
    if (waiter->future) {
        ecs_script_future_release(waiter->future);
    }
    ecs_os_zeromem(waiter);
}

static void biome_behavior_ctx_free(
    ecs_world_t *world,
    void *ptr)
{
    (void)world;
    BiomeBehaviorContext *ctx = ptr;
    int32_t count = ecs_vec_count(&ctx->alive_waiters);
    BiomeBehaviorAliveWaiter *waiters = ecs_vec_first_t(
        &ctx->alive_waiters, BiomeBehaviorAliveWaiter);
    for (int32_t i = 0; i < count; i ++) {
        biome_behavior_alive_waiter_fini(&waiters[i]);
    }
    ecs_vec_fini_t(
        NULL, &ctx->alive_waiters, BiomeBehaviorAliveWaiter);
    ecs_os_free(ctx);
}

static void BiomeBehaviorRuntime_fini(
    BiomeBehaviorRuntime *runtime)
{
    for (int32_t i = 0; i < runtime->count; i ++) {
        if (runtime->tasks[i]) {
            ecs_script_task_free(runtime->tasks[i]);
        }
    }
    ecs_os_free(runtime->tasks);
    ecs_os_zeromem(runtime);
}

ECS_CTOR(BiomeBehaviorRuntime, ptr, {
    ecs_os_zeromem(ptr);
})

ECS_MOVE(BiomeBehaviorRuntime, dst, src, {
    BiomeBehaviorRuntime_fini(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

ECS_DTOR(BiomeBehaviorRuntime, ptr, {
    BiomeBehaviorRuntime_fini(ptr);
})

static bool biome_behavior_resume(
    ecs_world_t *world,
    ecs_entity_t entity,
    ecs_script_task_t *task)
{
    ecs_script_eval_result_t result = {0};
    ecs_script_task_status_t status = ecs_script_task_resume(task, &result);
    if (status == EcsScriptTaskError) {
        char *path = ecs_get_path(world, entity);
        ecs_err("behavior for '%s' failed: %s",
            path ? path : "<anonymous>",
            result.error ? result.error : "unknown script error");
        ecs_os_free(path);
    }
    ecs_os_free(result.error);
    return status == EcsScriptTaskPending;
}

static void biome_behavior_resolve(
    ecs_script_future_t *future)
{
    int32_t result = 0;
    ecs_script_future_resolve(future,
        &(ecs_value_t){ecs_id(ecs_i32_t), &result});
}

static void biome_behavior_whileAlive(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    if (argc != 1) {
        ecs_script_future_reject(future, "whileAlive expects an entity");
        ecs_script_future_release(future);
        return;
    }

    ecs_entity_t target = *(ecs_entity_t*)argv[0].ptr;
    if (!ecs_is_alive(ctx->world, target)) {
        biome_behavior_resolve(future);
        ecs_script_future_release(future);
        return;
    }

    BiomeBehaviorContext *behavior_ctx = ctx->ctx;
    BiomeBehaviorAliveWaiter *waiter = ecs_vec_append_t(
        NULL, &behavior_ctx->alive_waiters, BiomeBehaviorAliveWaiter);
    *waiter = (BiomeBehaviorAliveWaiter){target, future};
}

static void biome_behavior_cancelWhileAlive(
    const ecs_function_ctx_t *ctx,
    ecs_script_future_t *future)
{
    BiomeBehaviorContext *behavior_ctx = ctx->ctx;
    int32_t count = ecs_vec_count(&behavior_ctx->alive_waiters);
    BiomeBehaviorAliveWaiter *waiters = ecs_vec_first_t(
        &behavior_ctx->alive_waiters, BiomeBehaviorAliveWaiter);
    for (int32_t i = 0; i < count; i ++) {
        if (waiters[i].future == future) {
            ecs_vec_remove_t(
                &behavior_ctx->alive_waiters,
                BiomeBehaviorAliveWaiter,
                i);
            ecs_script_future_release(future);
            return;
        }
    }
}

static void biome_behavior_delete(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    if (argc != 1) {
        ecs_script_future_reject(future, "delete expects an entity");
        ecs_script_future_release(future);
        return;
    }

    ecs_entity_t entity = *(ecs_entity_t*)argv[0].ptr;
    biome_behavior_resolve(future);
    ecs_script_future_release(future);
    if (ecs_is_alive(ctx->world, entity)) {
        ecs_delete(ctx->world, entity);
    }
}

static void BiomeBehaviorWaitWhileAlive(ecs_iter_t *it) {
    BiomeBehaviorContext *ctx = it->ctx;
    int32_t count = ecs_vec_count(&ctx->alive_waiters);
    BiomeBehaviorAliveWaiter *waiters = ecs_vec_first_t(
        &ctx->alive_waiters, BiomeBehaviorAliveWaiter);
    for (int32_t i = 0; i < count; ) {
        if (!ecs_is_alive(it->world, waiters[i].target)) {
            ecs_script_future_t *future = waiters[i].future;
            ecs_vec_remove_t(
                &ctx->alive_waiters, BiomeBehaviorAliveWaiter, i);
            biome_behavior_resolve(future);
            ecs_script_future_release(future);
            count --;
        } else {
            i ++;
        }
    }
}

static void BiomeBehaviorStart(ecs_iter_t *it) {
    const BiomeBehaviors *behaviors = ecs_field(it, BiomeBehaviors, 0);
    bool self = ecs_field_is_self(it, 0);

    for (int32_t i = 0; i < it->count; i ++) {
        const BiomeBehaviors *value = self ? &behaviors[i] : behaviors;
        int32_t count = ecs_vec_count(value);
        BiomeBehaviorRuntime runtime = {
            .tasks = ecs_os_calloc_n(ecs_script_task_t*, count),
            .count = count
        };
        ecs_entity_t *scripts = ecs_vec_first_t(value, ecs_entity_t);

        for (int32_t t = 0; t < count; t ++) {
            const EcsScript *script = ecs_get(it->world, scripts[t], EcsScript);
            if (!script || !script->script) {
                continue;
            }
            runtime.tasks[t] = ecs_script_task_new(script->script,
                &(ecs_script_task_desc_t){
                    .entity = it->entities[i],
                    .loop = EcsScriptTaskLoopForever
                });
            ecs_script_task_t *task = runtime.tasks[t];
            if (task && !biome_behavior_resume(
                it->world, it->entities[i], task))
            {
                ecs_script_task_free(task);
                runtime.tasks[t] = NULL;
            }
        }

        ecs_set_ptr(it->world, it->entities[i],
            BiomeBehaviorRuntime, &runtime);
    }
}

static void BiomeBehaviorUpdate(ecs_iter_t *it) {
    BiomeBehaviorRuntime *runtimes = ecs_field(
        it, BiomeBehaviorRuntime, 0);

    for (int32_t i = 0; i < it->count; i ++) {
        for (int32_t t = 0; t < runtimes[i].count; t ++) {
            ecs_script_task_t *task = runtimes[i].tasks[t];
            if (task && ecs_script_task_is_ready(task)) {
                if (!biome_behavior_resume(
                    it->world, it->entities[i], task))
                {
                    ecs_script_task_free(task);
                    runtimes[i].tasks[t] = NULL;
                }
            }
        }
    }
}

void biomeBehaviorImport(ecs_world_t *world) {
    ECS_MODULE(world, biomeBehavior);

    ecs_id(BiomeBehaviors) = ecs_vector(world, {
        .entity = ecs_entity(world, {
            .name = "Behaviors",
            .symbol = "BiomeBehaviors"
        }),
        .type = ecs_id(ecs_entity_t)
    });

    ecs_add_pair(world, ecs_id(BiomeBehaviors), EcsOnInstantiate, EcsInherit);

    ecs_set_name_prefix(world, "BiomeBehavior");

    ECS_COMPONENT_DEFINE(world, BiomeBehaviorRuntime);

    ecs_set_hooks(world, BiomeBehaviorRuntime, {
        .ctor = ecs_ctor(BiomeBehaviorRuntime),
        .move = ecs_move(BiomeBehaviorRuntime),
        .dtor = ecs_dtor(BiomeBehaviorRuntime),
        .flags = ECS_TYPE_HOOK_COPY_ILLEGAL
    });

    ecs_entity_t module = ecs_id(biomeBehavior);
    BiomeBehaviorContext *ctx = ecs_os_calloc_t(BiomeBehaviorContext);
    ecs_vec_init_t(
        NULL, &ctx->alive_waiters, BiomeBehaviorAliveWaiter, 0);
    ecs_atfini(world, biome_behavior_ctx_free, ctx);

    ecs_async_function(world, {
        .name = "whileAlive",
        .parent = module,
        .return_type = ecs_id(ecs_i32_t),
        .params = {{"entity", ecs_id(ecs_entity_t)}},
        .callback = biome_behavior_whileAlive,
        .cancel = biome_behavior_cancelWhileAlive,
        .ctx = ctx
    });

    ecs_async_function(world, {
        .name = "delete",
        .parent = module,
        .return_type = ecs_id(ecs_i32_t),
        .params = {{"entity", ecs_id(ecs_entity_t)}},
        .callback = biome_behavior_delete
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "WaitWhileAlive" }),
        .query.terms = {{
            .id = EcsModule,
            .src.id = module,
            .inout = EcsIn
        }},
        .phase = EcsPreUpdate,
        .callback = BiomeBehaviorWaitWhileAlive,
        .ctx = ctx
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "Start" }),
        .query.terms = {
            { .id = ecs_id(BiomeBehaviors), .inout = EcsIn },
            { .id = ecs_id(BiomeBehaviorRuntime), .oper = EcsNot }
        },
        .phase = EcsPreUpdate,
        .callback = BiomeBehaviorStart
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "Update" }),
        .query.terms = {
            { .id = ecs_id(BiomeBehaviorRuntime) }
        },
        .phase = EcsPreUpdate,
        .callback = BiomeBehaviorUpdate
    });
}
