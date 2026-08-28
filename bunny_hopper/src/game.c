#define BUNNY_HOPPER_GAME_IMPL
#include "bunny_hopper.h"

float randf(void) {
    return (float)rand() / (float)RAND_MAX;
}

void bunnySpawnCarrot(ecs_world_t *world, const BunnyGame *game, bool large) {
    ecs_entity_t prefab = large ? game->carrot_large_prefab : game->carrot_small_prefab;

    float spawn_x = ecs_const_var_get_t(world, "cfg.spawnX", ecs_f32_t);
    float ground_y = ecs_const_var_get_t(world, "cfg.groundY", ecs_f32_t);

    ecs_entity_t carrot = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_add_pair(world, carrot, EcsChildOf, ecs_lookup(world, "carrots"));
    ecs_set(world, carrot, FlecsPosition3, {spawn_x, ground_y, 0});
    ecs_add(world, carrot, FlecsDynamicTransform);
}

void bunnySetState(ecs_world_t *world, BunnyGameState state) {
    ecs_singleton_add_pair(world, BunnyGameState, 
        ecs_constant_to_entity(world, BunnyGameState, state));
}

static void bunnyResolve(ecs_script_future_t *future, bool value) {
    ecs_script_future_resolve(future,
        &(ecs_value_t){ecs_id(ecs_bool_t), &value});
    ecs_script_future_release(future);
}

static bool bunnyPlaying(ecs_world_t *world) {
    return ecs_has_pair(world, ecs_id(BunnyGameState), ecs_id(BunnyGameState),
        ecs_constant_to_entity(world, BunnyGameState, BunnyGameStatePlaying));
}

static void bunnyJump(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    (void)argc;
    (void)argv;
    ecs_world_t *world = ctx->world;
    const BunnyGame *game = ecs_singleton_get(world, BunnyGame);
    BunnyJump *jump = game ? ecs_get_mut(world, game->player, BunnyJump) : NULL;
    if (!jump || !bunnyPlaying(world)) {
        bunnyResolve(future, false);
        return;
    }

    if (jump->jumps >= ecs_const_var_get_t(world, "cfg.maxJumps", ecs_i32_t)) {
        bunnyResolve(future, false);
        return;
    }

    jump->v = ecs_const_var_get_t(world, "cfg.jumpVelocity", ecs_f32_t);
    jump->jumps ++;
    ecs_modified(world, game->player, BunnyJump);
    bunnyResolve(future, true);
}

static void bunnyRestart(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    (void)argc;
    (void)argv;
    ecs_world_t *world = ctx->world;
    if (bunnyPlaying(world)) {
        bunnyResolve(future, false);
        return;
    }

    ecs_delete_with(world, ecs_pair(EcsChildOf, ecs_lookup(world, "carrots")));

    if (!ecs_script(world, { .filename = "etc/scene.flecs" })) {
        ecs_err("failed to reload etc/scene.flecs");
    }
    bunnyResolve(future, true);
}

void BunnyPhysics(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    BunnyPlayer *player = ecs_field(it, BunnyPlayer, 0);
    BunnyJump *jump = ecs_field(it, BunnyJump, 1);
    FlecsPosition3 *pos = ecs_field(it, FlecsPosition3, 2);
    FlecsRotation3 *rot = ecs_field(it, FlecsRotation3, 3);
    const BunnyGame *game = ecs_field(it, BunnyGame, 4);

    ecs_entity_t state = ecs_pair_second(it->real_world, ecs_field_id(it, 5));
    bool playing = state == ecs_constant_to_entity(world, BunnyGameState, BunnyGameStatePlaying);
    bool over = state == ecs_constant_to_entity(world, BunnyGameState, BunnyGameStateOver);

    float ground_y = ecs_const_var_get_t(world, "cfg.groundY", ecs_f32_t);
    float dead_height = ecs_const_var_get_t(world, "cfg.deadHeight", ecs_f32_t);
    float gravity = ecs_const_var_get_t(world, "cfg.gravity", ecs_f32_t);
    float hard_landing_velocity = ecs_const_var_get_t(world, "cfg.hardLandingVelocity", ecs_f32_t);
    float speed = ecs_mut_var_get_t(world, "speed", ecs_f32_t);
    float ground = over ? ground_y + dead_height : ground_y;

    for (int i = 0; i < it->count; i ++) {
        jump[i].v += gravity * it->delta_time;
        pos[i].y += jump[i].v * it->delta_time;

        if (pos[i].y <= ground) {
            if (jump[i].jumps > 0) {
                ecs_entity_t effect = game->dust_burst_low;
                if (-jump[i].v > hard_landing_velocity) {
                    effect = game->dust_burst_high;
                }

                const FlecsParticleBurst *dust = ecs_get(
                    it->world, effect, FlecsParticleBurst);
                float feet[3] = {pos[i].x, 0.0f, 0.0f};
                flecsEngine_particlesBurst(it->world, game->dust_pool, feet, dust);
            }

            pos[i].y = ground;
            jump[i].v = 0;
            jump[i].jumps = 0;
        }

        if (playing && jump[i].jumps == 0) {
            player[i].run_phase += it->delta_time * (4.0f + speed * 0.9f);
        }

        float tilt = jump[i].v * 0.025f;
        if (tilt > 0.35f) tilt = 0.35f;
        if (tilt < -0.35f) tilt = -0.35f;
        rot[i].z = over ? ecs_const_var_get_t(world, "cfg.deadAngle", ecs_f32_t) : tilt;
    }
}

void BunnyInvulnerability(ecs_iter_t *it) {
    BunnyInvulnerable *invuln = ecs_field(it, BunnyInvulnerable, 0);

    for (int i = 0; i < it->count; i ++) {
        invuln[i].time_left -= it->delta_time;
        if (invuln[i].time_left <= 0) {
            ecs_remove(it->world, it->entities[i], BunnyInvulnerable);
        }
    }
}

void BunnyLegs(ecs_iter_t *it) {
    const BunnyLeg *leg = ecs_field(it, BunnyLeg, 0);
    FlecsRotation3 *rot = ecs_field(it, FlecsRotation3, 1);
    const BunnyGame *game = ecs_field(it, BunnyGame, 2);
    ecs_entity_t state = ecs_pair_second(it->world, ecs_field_id(it, 3));

    const BunnyPlayer *player = ecs_get(it->world, game->player, BunnyPlayer);
    const BunnyJump *jump = ecs_get(it->world, game->player, BunnyJump);
    if (!player || !jump) {
        return;
    }

    float alpha = it->delta_time * 14.0f;
    if (alpha > 1.0f) {
        alpha = 1.0f;
    }

    for (int i = 0; i < it->count; i ++) {
        float target;
        if (state != ecs_constant_to_entity(it->world, BunnyGameState, BunnyGameStatePlaying)) {
            target = 0;
        } else if (jump->jumps > 0) {
            target = leg[i].air_target;
        } else {
            target = sinf(player->run_phase + leg[i].phase) * 0.55f;
        }
        rot[i].z += (target - rot[i].z) * alpha;
    }
}

void BunnyCarrotScroll(ecs_iter_t *it) {
    FlecsPosition3 *pos = ecs_field(it, FlecsPosition3, 1);
    BunnyGame *game = ecs_field(it, BunnyGame, 2);
    float speed = ecs_mut_var_get_t(it->world, "speed", ecs_f32_t);
    float despawn_x = ecs_const_var_get_t(it->world, "cfg.despawnX", ecs_f32_t);

    for (int i = 0; i < it->count; i ++) {
        pos[i].x -= speed * it->delta_time;
        if (pos[i].x < despawn_x) {
            const FlecsParticleBurst *burst = game->sparkle_burst ? ecs_get(
                it->world, game->sparkle_burst, FlecsParticleBurst) : NULL;
            if (game->sparkle_pool && burst) {
                float at[3] = {pos[i].x, pos[i].y, 0.0f};
                flecsEngine_particlesBurst(
                    it->world, game->sparkle_pool, at, burst);
            }

            ecs_delete(it->world, it->entities[i]);

            game->carrots ++;

            if (!(game->carrots % 5)) {
                float new_speed = ecs_mut_var_get_t(it->world, "speed", ecs_f32_t) + 2.0f;
                ecs_mut_var_set_t(it->world, "speed", ecs_f32_t, {new_speed});
                ecs_set(it->world, game->player, Bunny, { .speed = new_speed });
            }

            ecs_singleton_modified(it->world, BunnyGame);
        }
    }
}

void BunnySceneryScroll(ecs_iter_t *it) {
    const BunnyScenery *scenery = ecs_field(it, BunnyScenery, 0);
    FlecsPosition3 *pos = ecs_field(it, FlecsPosition3, 1);
    float speed = ecs_mut_var_get_t(it->world, "speed", ecs_f32_t);

    for (int i = 0; i < it->count; i ++) {
        pos[i].x -= speed * scenery[i].parallax * it->delta_time;
        if (pos[i].x < scenery[i].wrap_min) {
            pos[i].x += scenery[i].wrap_span;
        }
    }
}

void bunnyScrollPool(ecs_world_t *world, ecs_entity_t pool_entity,
    float shift)
{
    if (!pool_entity) {
        return;
    }

    FlecsParticles *pool = ecs_get_mut(world, pool_entity, FlecsParticles);
    if (!pool || !pool->particles) {
        return;
    }

    for (int i = 0; i < pool->count; i ++) {
        pool->particles[i].pos[0] -= shift;
    }
}

void BunnyDustScroll(ecs_iter_t *it) {
    const BunnyGame *game = ecs_field(it, BunnyGame, 0);

    float speed = ecs_mut_var_get_t(it->world, "speed", ecs_f32_t);
    bunnyScrollPool(it->world, game->dust_pool, speed * it->delta_time);
    bunnyScrollPool(it->world, game->hit_pool, speed * it->delta_time);
    bunnyScrollPool(it->world, game->sparkle_pool, speed * it->delta_time);
}

void BunnySpawn(ecs_iter_t *it) {
    BunnyGame *game = ecs_field(it, BunnyGame, 0);
    float speed = ecs_mut_var_get_t(it->world, "speed", ecs_f32_t);

    game->distance_since_spawn += speed * it->delta_time;
    if (game->distance_since_spawn < game->next_spawn_distance) {
        return;
    }

    float large_carrot_chance = 
        ecs_const_var_get_t(it->world, "cfg.largeCarrotChance", ecs_f32_t);
    bool large = game->carrots >= 5 && randf() < large_carrot_chance;
    bunnySpawnCarrot(it->world, game, large);

    game->distance_since_spawn = 0;
    game->next_spawn_distance = speed * (1 + randf());
}

void BunnyCollide(ecs_iter_t *it) {
    BunnyCarrot *carrot = ecs_field(it, BunnyCarrot, 0);
    const FlecsPosition3 *pos = ecs_field(it, FlecsPosition3, 1);
    BunnyGame *game = ecs_field(it, BunnyGame, 2);

    const FlecsPosition3 *ppos = ecs_get(it->world, game->player, FlecsPosition3);
    BunnyJump *jump = ecs_get_mut(it->world, game->player, BunnyJump);
    BunnyLives *lives = ecs_get_mut(it->world, game->player, BunnyLives);
    bool invuln = ecs_has(it->world, game->player, BunnyInvulnerable);

    float px0 = ppos->x - 0.55f;
    float px1 = ppos->x + 0.8f;
    float py0 = ppos->y + 0.05f;
    float py1 = ppos->y + 1.3f;

    for (int i = 0; i < it->count; i ++) {
        float hw = carrot[i].half_width * 0.85f;
        float cx0 = pos[i].x - hw;
        float cx1 = pos[i].x + hw;
        float cy1 = carrot[i].height * 0.9f;

        if (!invuln && px0 < cx1 && px1 > cx0 && py0 < cy1 && py1 > 0) {
            lives->lives_left --;
            ecs_modified(it->world, game->player, BunnyLives);

            const FlecsParticleBurst *burst = game->hit_burst ? ecs_get(
                it->world, game->hit_burst, FlecsParticleBurst) : NULL;
            if (game->hit_pool && burst) {
                float impact[3] = {ppos->x, ppos->y, 0.0f};
                flecsEngine_particlesBurst(
                    it->world, game->hit_pool, impact, burst);
            }

            if (lives->lives_left <= 0) {
                bunnySetState(it->world, BunnyGameStateOver);
                ecs_set(it->world, game->player, Bunny, { .speed = 0, .dead = true });
                break;
            }

            if (jump) {
                jump->v = ecs_const_var_get_t(it->world, "cfg.jumpVelocity", ecs_f32_t);
                jump->jumps = 1;
            }

            ecs_set(it->world, game->player, BunnyInvulnerable, {
                ecs_const_var_get_t(it->world, "cfg.invulnTime", ecs_f32_t)
            });

            invuln = true;
        }
    }
}

void Bunny_hopperImport(ecs_world_t *world) {
    ECS_MODULE(world, Bunny_hopper);

    ECS_META_COMPONENT(world, BunnyGameState);
    ECS_META_COMPONENT(world, BunnyPlayer);
    ECS_META_COMPONENT(world, BunnyJump);
    ECS_META_COMPONENT(world, BunnyLives);
    ECS_META_COMPONENT(world, BunnyInvulnerable);
    ECS_META_COMPONENT(world, BunnyGame);
    ECS_META_COMPONENT(world, BunnyLeg);
    ECS_META_COMPONENT(world, BunnyCarrot);
    ECS_META_COMPONENT(world, BunnyScenery);
    ECS_META_COMPONENT(world, Bunny);

    ecs_add_id(world, ecs_id(BunnyGameState), EcsExclusive);
    ecs_add_id(world, ecs_id(BunnyGameState), EcsSingleton);
    ecs_add_id(world, ecs_id(BunnyGame), EcsSingleton);

    ecs_async_function(world, {
        .name = "jump",
        .parent = ecs_id(Bunny_hopper),
        .return_type = ecs_id(ecs_bool_t),
        .callback = bunnyJump
    });

    ecs_async_function(world, {
        .name = "restart",
        .parent = ecs_id(Bunny_hopper),
        .return_type = ecs_id(ecs_bool_t),
        .callback = bunnyRestart
    });

    ecs_entity_t statePlaying = ecs_constant_to_entity(
        world, BunnyGameState, BunnyGameStatePlaying);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BunnyPhysics" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(BunnyPlayer) },
            { .id = ecs_id(BunnyJump) },
            { .id = ecs_id(FlecsPosition3) },
            { .id = ecs_id(FlecsRotation3) },
            { .id = ecs_id(BunnyGame) },
            { .id = ecs_pair_t(BunnyGameState, EcsWildcard) }
        },
        .callback = BunnyPhysics
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BunnyInvulnerability" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(BunnyInvulnerable) },
            { .id = ecs_pair_t(BunnyGameState, statePlaying) }
        },
        .callback = BunnyInvulnerability
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BunnyLegs" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(BunnyLeg) },
            { .id = ecs_id(FlecsRotation3) },
            { .id = ecs_id(BunnyGame) },
            { .id = ecs_pair_t(BunnyGameState, EcsWildcard) }
        },
        .callback = BunnyLegs
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BunnyCarrotScroll" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(BunnyCarrot) },
            { .id = ecs_id(FlecsPosition3) },
            { .id = ecs_id(BunnyGame) },
            { .id = ecs_pair_t(BunnyGameState, statePlaying) }
        },
        .callback = BunnyCarrotScroll
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BunnySceneryScroll" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(BunnyScenery) },
            { .id = ecs_id(FlecsPosition3) },
            { .id = ecs_pair_t(BunnyGameState, statePlaying) }
        },
        .callback = BunnySceneryScroll
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BunnyDustScroll" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(BunnyGame) },
            { .id = ecs_pair_t(BunnyGameState, statePlaying) }
        },
        .callback = BunnyDustScroll
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BunnySpawn" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(BunnyGame) },
            { .id = ecs_pair_t(BunnyGameState, statePlaying) }
        },
        .callback = BunnySpawn
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BunnyCollide" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(BunnyCarrot) },
            { .id = ecs_id(FlecsPosition3) },
            { .id = ecs_id(BunnyGame) },
            { .id = ecs_pair_t(BunnyGameState, statePlaying) }
        },
        .callback = BunnyCollide
    });
}
