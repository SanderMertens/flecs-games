#define ORE_ELSE_PLAYER_IMPL
#include "ore_else.h"

static ecs_entity_t oreCursorTarget(
    ecs_world_t *world,
    OreGame *game,
    const FlecsInput *input,
    int32_t *row_out,
    int32_t *col_out,
    bool *building_out)
{
    OreCell cell;
    if (!oreCellAt(world, game, input->mouse.view_norm.x,
        input->mouse.view_norm.y, &cell))
    {
        return 0;
    }

    ecs_entity_t target = *oreBuildingCell(game, cell.row, cell.col);
    *building_out = target != 0;

    if (!target) {
        target = *oreDepositCell(game, cell.row, cell.col);
    }

    if (!target) {
        return 0;
    }

    *row_out = cell.row;
    *col_out = cell.col;

    return target;
}

static void OrePlayerMove(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    OrePlayer *player = ecs_field(it, OrePlayer, 0);
    FlecsPosition3 *pos = ecs_field(it, FlecsPosition3, 1);
    FlecsRotation3 *rot = ecs_field(it, FlecsRotation3, 2);
    const OrePlayerIntent *intent = ecs_field(it, OrePlayerIntent, 3);
    OreGame *game = ecs_field(it, OreGame, 4);

    const OreRocketState *rocket = ecs_singleton_get(world, OreRocketState);

    if (rocket && rocket->boarded) {
        return;
    }

    OreMap map = ecs_const_var_get_t(world, "cfg.map", OreMap);
    float speed = ecs_const_var_get_t(world, "cfg.walkSpeed", ecs_f32_t);
    float bob = ecs_const_var_get_t(world, "cfg.bobHeight", ecs_f32_t);
    float step_rate = ecs_const_var_get_t(world, "cfg.stepRate", ecs_f32_t);
    float run = ecs_const_var_get_t(world, "cfg.runMultiplier", ecs_f32_t);
    float hover_h = ecs_const_var_get_t(world, "cfg.hoverHeight", ecs_f32_t);
    float rise = ecs_const_var_get_t(world, "cfg.hoverRise", ecs_f32_t);
    float fall = ecs_const_var_get_t(world, "cfg.hoverFall", ecs_f32_t);

    bool running = intent->run;

    if (running && run > 1.0f) {
        speed *= run;
    }

    float min_x = oreTileX(&map, 0);
    float max_x = oreTileX(&map, ORE_COLS - 1);
    float min_z = oreTileZ(&map, 0);
    float max_z = oreTileZ(&map, ORE_ROWS - 1);

    vec3 forward = {0, 0, 1}, right = {1, 0, 0};
    if (!flecsEngine_cameraViewBasis(world, game->camera, forward, right, NULL)) {
        glm_vec3_copy((vec3){0, 0, 1}, forward);
        glm_vec3_copy((vec3){1, 0, 0}, right);
    }

    forward[1] = 0;
    right[1] = 0;

    if (glm_vec3_norm(forward) < 0.001f) {
        glm_vec3_copy((vec3){0, 0, 1}, forward);
    }
    if (glm_vec3_norm(right) < 0.001f) {
        glm_vec3_copy((vec3){1, 0, 0}, right);
    }

    glm_vec3_normalize(forward);
    glm_vec3_normalize(right);

    float fx = 0, fz = 0;
    if (intent->forward) fz += 1;
    if (intent->back) fz -= 1;
    if (intent->right) fx += 1;
    if (intent->left) fx -= 1;

    float dx = forward[0] * fz + right[0] * fx;
    float dz = forward[2] * fz + right[2] * fx;
    float len = sqrtf(dx * dx + dz * dz);

    for (int i = 0; i < it->count; i ++) {
        player[i].moving = len > 0.001f;
        player[i].running = running && player[i].moving;

        if (player[i].moving) {
            dx /= len;
            dz /= len;

            pos[i].x += dx * speed * it->delta_time;
            pos[i].z += dz * speed * it->delta_time;

            rot[i].y = atan2f(dx, dz);

            player[i].anim += it->delta_time * speed * step_rate;
        }

        if (pos[i].x < min_x) pos[i].x = min_x;
        if (pos[i].x > max_x) pos[i].x = max_x;
        if (pos[i].z < min_z) pos[i].z = min_z;
        if (pos[i].z > max_z) pos[i].z = max_z;

        int32_t col = oreColAt(&map, pos[i].x);
        int32_t row = oreRowAt(&map, pos[i].z);

        bool over = oreOnGrid(row, col) &&
            *oreBuildingCell(game, row, col) != 0;

        float target = over ? hover_h : 0;
        float rate = (target > player[i].hover ? rise : fall) * it->delta_time;

        if (player[i].hover < target) {
            player[i].hover += rate;
            if (player[i].hover > target) {
                player[i].hover = target;
            }
        } else if (player[i].hover > target) {
            player[i].hover -= rate;
            if (player[i].hover < target) {
                player[i].hover = target;
            }
        }

        player[i].airborne = player[i].hover > 0.01f;

        pos[i].y = player[i].hover;

        if (player[i].moving && !player[i].airborne) {
            pos[i].y += fabsf(sinf(player[i].anim)) * bob;
        }

        if (!player[i].jet) {
            player[i].jet = ecs_lookup_child(world, it->entities[i], "jet");
        }

        oreFxToggle(world, player[i].jet, player[i].airborne);

        float phase = floorf(player[i].anim * (float)(1.0 / M_PI));

        if (phase == player[i].step) {
            continue;
        }

        player[i].step = phase;

        if (player[i].moving && !player[i].airborne) {
            oreBurst(world, game->fx_pool, player[i].dust,
                pos[i].x, 0.04f, pos[i].z);
        }
    }
}

static void oreMineProgressSet(
    ecs_world_t *world,
    ecs_entity_t e,
    OreMineProgress *mine,
    float value)
{
    if (mine->value == value) {
        return;
    }

    mine->value = value;

    ecs_modified(world, e, OreMineProgress);
}

static void OrePlayerMine(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    OrePlayer *player = ecs_field(it, OrePlayer, 0);
    const FlecsPosition3 *pos = ecs_field(it, FlecsPosition3, 1);
    const OrePlayerIntent *intent = ecs_field(it, OrePlayerIntent, 2);
    const FlecsInput *input = ecs_field(it, FlecsInput, 3);
    OreGame *game = ecs_field(it, OreGame, 4);
    OreMineProgress *mine = ecs_field(it, OreMineProgress, 5);

    OreMap map = ecs_const_var_get_t(world, "cfg.map", OreMap);
    float range = ecs_const_var_get_t(world, "cfg.reach", ecs_f32_t);

    const OreRocketState *rocket = ecs_singleton_get(world, OreRocketState);
    const OreUiState *ui = ecs_singleton_get(world, OreUiState);

    bool holding = intent->mine && !(rocket && rocket->boarded) &&
        !(ui && ui->open) && !flecsEngine_uiMouseCaptured(world);

    int32_t row = 0, col = 0;
    bool building = false;

    ecs_entity_t target = holding
        ? oreCursorTarget(world, game, input, &row, &col, &building)
        : 0;

    float tx = target ? oreTileX(&map, col) : 0;
    float tz = target ? oreTileZ(&map, row) : 0;

    for (int i = 0; i < it->count; i ++) {
        if (!target) {
            player[i].mine_target = 0;
            player[i].mine_left = 0;
            oreMineProgressSet(world, it->entities[i], &mine[i], 0);
            continue;
        }

        float dx = tx - pos[i].x;
        float dz = tz - pos[i].z;
        bool in_range = (dx * dx + dz * dz) <= range * range;

        flecsEngine_draw(world,
            in_range ? game->marker_prefab : game->marker_bad,
            &(flecs_draw_instance_t){
                .position = {tx, 0.14f, tz},
                .scale = {1, 1, 1}
            }, 1);

        if (!in_range) {
            player[i].mine_target = 0;
            player[i].mine_left = 0;
            oreMineProgressSet(world, it->entities[i], &mine[i], 0);
            continue;
        }

        float action_time;

        if (building) {
            action_time = ecs_const_var_get_t(
                world, "cfg.dozeTime", ecs_f32_t);
        } else {
            const OreDeposit *deposit = ecs_get(world, target, OreDeposit);
            const OreResource *resource = deposit
                ? ecs_get(world, deposit->resource, OreResource)
                : NULL;
            if (!resource) {
                continue;
            }

            action_time = (float)resource->mine_time_ds * 0.1f;
        }

        if (action_time <= 0) {
            action_time = 1.0f;
        }

        if (player[i].mine_target != target) {
            player[i].mine_target = target;
            player[i].mine_left = action_time;
        }

        player[i].mine_left -= it->delta_time;
        oreMineProgressSet(world, it->entities[i], &mine[i],
            1.0f - player[i].mine_left / action_time);

        if (player[i].mine_left > 0) {
            continue;
        }

        player[i].mine_left = action_time;
        oreMineProgressSet(world, it->entities[i], &mine[i], 0);

        if (building) {
            oreDemolish(world, game, &map, row, col);
            player[i].mine_target = 0;
        } else if (!oreDepositMine(world, game, target)) {
            player[i].mine_target = 0;
        }
    }
}

static void OreCameraFollow(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    const OreGame *game = ecs_field(it, OreGame, 0);
    const FlecsInput *input = ecs_field(it, FlecsInput, 1);
    OreViewState *view = ecs_field(it, OreViewState, 2);

    if (!game->camera || !game->player) {
        return;
    }

    const FlecsPosition3 *pos = ecs_get(world, game->player, FlecsPosition3);
    if (!pos) {
        return;
    }

    OreZoom zoom = ecs_const_var_get_t(world, "cfg.zoom", OreZoom);

    int32_t level = view->level;

    if (input->mouse.scroll.y != 0 && !flecsEngine_uiMouseCaptured(world)) {
        level += input->mouse.scroll.y > 0 ? -1 : 1;
    }

    if (level < 0) {
        level = 0;
    }
    if (level > 2) {
        level = 2;
    }

    if (view->level != level) {
        view->level = level;
        ecs_singleton_modified(world, OreViewState);
    }

    float height = zoom.height[level];
    float back = zoom.back[level];
    float look = zoom.look[level];

    if (view->height <= 0) {
        view->height = height;
        view->back = back;
        view->look = look;
    } else {
        float alpha = zoom.glide * it->delta_time;
        if (alpha > 1.0f) {
            alpha = 1.0f;
        }

        view->height += (height - view->height) * alpha;
        view->back += (back - view->back) * alpha;
        view->look += (look - view->look) * alpha;
    }

    ecs_set(world, game->camera, FlecsPosition3, {
        pos->x, view->height, pos->z + view->back});
    ecs_set(world, game->camera, FlecsLookAt, {
        pos->x, view->look, pos->z});
}

static void OreLimbs(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    const OreLimb *limb = ecs_field(it, OreLimb, 0);
    FlecsRotation3 *rot = ecs_field(it, FlecsRotation3, 1);
    const OreClock *clock = ecs_field(it, OreClock, 2);
    const EcsParent *parents = ecs_field(it, EcsParent, 3);

    float blend = ecs_const_var_get_t(world, "cfg.limbBlend", ecs_f32_t);
    float trail = ecs_const_var_get_t(world, "cfg.airTrail", ecs_f32_t);
    float trail_max = ecs_const_var_get_t(world, "cfg.airTrailMax", ecs_f32_t);
    float dangle = ecs_const_var_get_t(world, "cfg.airDangle", ecs_f32_t);
    float dangle_rate = ecs_const_var_get_t(
        world, "cfg.airDangleRate", ecs_f32_t);
    float run = ecs_const_var_get_t(world, "cfg.runMultiplier", ecs_f32_t);

    float alpha = it->delta_time * blend;
    if (alpha > 1.0f) {
        alpha = 1.0f;
    }

    ecs_entity_t last_parent = 0;
    const OrePlayer *player = NULL;
    const OreCritter *critter = NULL;

    for (int i = 0; i < it->count; i ++) {
        ecs_entity_t parent = parents[i].value;
        if (parent != last_parent) {
            last_parent = parent;
            player = ecs_get(world, parent, OrePlayer);
            critter = player ? NULL : ecs_get(world, parent, OreCritter);
        }

        if (!player) {
            if (critter) {
                float swing = critter->attacking ? 0.35f : 1.0f;
                rot[i].x = sinf(critter->anim + limb[i].phase) *
                    limb[i].swing * swing;
            }
            continue;
        }

        float target;

        if (player->airborne) {
            float lag = 0;

            if (player->moving) {
                lag = trail * (player->running ? run : 1.0f);
                if (lag > trail_max) {
                    lag = trail_max;
                }
            }

            target = (lag +
                sinf(clock->time_elapsed * dangle_rate + limb[i].phase) *
                dangle) * limb[i].swing;
        } else {
            target = player->moving
                ? sinf(player->anim + limb[i].phase) * limb[i].swing
                : 0;
        }

        rot[i].x += (target - rot[i].x) * alpha;
    }
}

void orePlayerImport(ecs_world_t *world) {
    ECS_META_COMPONENT(world, OreZoom);
    ECS_META_COMPONENT(world, OreViewState);
    ECS_META_COMPONENT(world, OrePlayer);
    ECS_META_COMPONENT(world, OrePlayerIntent);
    ECS_META_COMPONENT(world, OreMineProgress);
    ECS_META_COMPONENT(world, OreLimb);

    ecs_add_id(world, ecs_id(OreViewState), EcsSingleton);

    ecs_singleton_ensure(world, OreViewState);
    ecs_singleton_modified(world, OreViewState);

    ecs_add_pair(world, ecs_id(OrePlayer), EcsWith, FlecsDynamicTransform);
    ecs_add_pair(world, ecs_id(OrePlayer), EcsWith, ecs_id(OrePlayerIntent));
    ecs_add_pair(world, ecs_id(OrePlayer), EcsWith, ecs_id(OreMineProgress));
    ecs_add_pair(world, ecs_id(OreLimb), EcsWith, ecs_id(FlecsRotation3));

    ecs_entity_t playing = ecs_constant_to_entity(
        world, OreGameState, OreGameStatePlaying);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OrePlayerMove" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OrePlayer) },
            { .id = ecs_id(FlecsPosition3) },
            { .id = ecs_id(FlecsRotation3) },
            { .id = ecs_id(OrePlayerIntent), .inout = EcsIn },
            { .id = ecs_id(OreGame) },
            { .id = ecs_pair_t(OreGameState, playing) }
        },
        .callback = OrePlayerMove
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OrePlayerMine" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OrePlayer) },
            { .id = ecs_id(FlecsPosition3) },
            { .id = ecs_id(OrePlayerIntent), .inout = EcsIn },
            { .id = ecs_id(FlecsInput) },
            { .id = ecs_id(OreGame) },
            { .id = ecs_id(OreMineProgress) },
            { .id = ecs_pair_t(OreGameState, playing) }
        },
        .callback = OrePlayerMine
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreCameraFollow" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreGame) },
            { .id = ecs_id(FlecsInput) },
            { .id = ecs_id(OreViewState) }
        },
        .callback = OreCameraFollow
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreLimbs" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreLimb) },
            { .id = ecs_id(FlecsRotation3) },
            { .id = ecs_id(OreClock), .inout = EcsIn },
            { .id = ecs_id(EcsParent), .inout = EcsIn }
        },
        .callback = OreLimbs
    });
}
