#define LETTUCE_PRAY_GAME_IMPL
#include "lettuce_pray.h"

ECS_TAG_DECLARE(LettuceProducer);

float lettuceRandf(void) {
    return (float)rand() / (float)RAND_MAX;
}

float lettuceTileX(const LettuceLawn *lawn, int32_t col) {
    return lawn->x0 + (float)col * lawn->tile;
}

float lettuceTileZ(const LettuceLawn *lawn, int32_t row) {
    return lawn->z0 + (float)row * lawn->tile;
}

int32_t lettuceColAt(const LettuceLawn *lawn, float x) {
    return (int32_t)floorf((x - lawn->x0) / lawn->tile + 0.5f);
}

int32_t lettuceRowAt(const LettuceLawn *lawn, float z) {
    return (int32_t)floorf((z - lawn->z0) / lawn->tile + 0.5f);
}

bool lettuceOnLawn(int32_t row, int32_t col) {
    return row >= 0 && row < LETTUCE_ROWS && col >= 0 && col < LETTUCE_COLS;
}

ecs_entity_t* lettuceCell(LettuceGame *game, int32_t row, int32_t col) {
    return &game->grid[row * LETTUCE_COLS + col];
}

void lettuceBurst(ecs_world_t *world, ecs_entity_t pool, ecs_entity_t burst,
    float x, float y, float z)
{
    const FlecsParticleBurst *b = ecs_get(world, burst, FlecsParticleBurst);
    float at[3] = {x, y, z};
    flecsEngine_particlesBurst(world, pool, at, b);
}

void lettuceSetState(ecs_world_t *world, LettuceGameState state) {
    ecs_singleton_add_pair(world, LettuceGameState,
        ecs_constant_to_entity(world, LettuceGameState, state));
}

void lettuceSpawnSun(ecs_world_t *world, const LettuceGame *game, float x,
    float y, float z)
{
    ecs_entity_t sun = ecs_new_w_pair(world, EcsIsA, game->sun_prefab);
    ecs_add_pair(world, sun, EcsChildOf, game->suns);
    ecs_add(world, sun, FlecsDynamicTransform);
    ecs_set(world, sun, FlecsPosition3, {x, y, z});
    ecs_set(world, sun, FlecsRotation3, {0, 0, 0});
    ecs_set(world, sun, FlecsScale3, {1, 1, 1});
}

void LettucePlantOnRemove(ecs_iter_t *it) {
    const LettucePlant *plant = ecs_field(it, LettucePlant, 0);
    LettuceGame *game = ecs_field(it, LettuceGame, 1);

    for (int i = 0; i < it->count; i ++) {
        ecs_entity_t *cell = lettuceCell(game, plant[i].row, plant[i].col);
        if (*cell == it->entities[i]) {
            *cell = 0;
        }
    }
}

bool lettuceDig(ecs_world_t *world, LettuceGame *game, const LettuceLawn *lawn,
    int32_t row, int32_t col)
{
    ecs_entity_t *cell = lettuceCell(game, row, col);
    if (!*cell) {
        return false;
    }

    float x = lettuceTileX(lawn, col);
    float z = lettuceTileZ(lawn, row);
    lettuceBurst(world, game->fx_pool, game->chomp_burst, x, 0.5f, z);

    ecs_delete(world, *cell);
    return true;
}

void lettuceKillZombie(ecs_world_t *world, LettuceGame *game, ecs_entity_t e,
    LettuceZombie *z, LettuceHealth *health, const FlecsPosition3 *pos)
{
    health->value = 0;
    z->eating = 0;

    ecs_remove(world, e, LettuceZombie);
    ecs_set(world, e, LettuceCorpse, {0});

    lettuceBurst(world, game->fx_pool, game->splat_burst,
        pos->x, pos->y + 1.1f, pos->z);

    game->killed ++;
}

void lettuceDamageZombie(ecs_world_t *world, LettuceGame *game, ecs_entity_t e,
    LettuceZombie *z, LettuceHealth *health, LettuceArmor *armor,
    const FlecsPosition3 *pos, float damage)
{
    if (health->value <= 0) {
        return;
    }

    if (armor && armor->value > 0) {
        armor->value -= damage;
        if (armor->value <= 0) {
            armor->value = 0;

            ecs_delete(world, ecs_lookup_child(world, e, "armor"));

            lettuceBurst(world, game->fx_pool, game->armor_burst,
                pos->x, pos->y + 2.0f, pos->z);
        }
        return;
    }

    health->value -= damage;
    if (health->value <= 0) {
        lettuceKillZombie(world, game, e, z, health, pos);
    }
}

void LettuceResetGrid(ecs_iter_t *it) {
    LettuceGame *game = ecs_field(it, LettuceGame, 0);

    for (int i = 0; i < LETTUCE_ROWS; i ++) {
        game->row_max[i] = -1000.0f;
        game->row_min[i] = 1000.0f;
    }
}

static void LettuceUiClick(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    ecs_entity_t playing = ecs_constant_to_entity(
        world, LettuceGameState, LettuceGameStatePlaying);
    if (!ecs_has_pair(world, ecs_id(LettuceGameState),
        ecs_id(LettuceGameState), playing))
    {
        return;
    }

    LettuceSeedPacket *packet = ecs_field(it, LettuceSeedPacket, 0);
    LettuceGame *game = ecs_singleton_ensure(world, LettuceGame);

    for (int i = 0; i < it->count; i ++) {
        if (!packet[i].plant) {
            game->shovel = !game->shovel;
            game->selected = 0;
        } else {
            game->selected = (game->selected == packet[i].plant) ?
                0 : packet[i].plant;
            game->shovel = false;
        }
    }

    ecs_singleton_modified(world, LettuceGame);
}

static void lettuceResolve(ecs_script_future_t *future, bool value) {
    ecs_script_future_resolve(future,
        &(ecs_value_t){ecs_id(ecs_bool_t), &value});
    ecs_script_future_release(future);
}

static bool lettucePlaying(ecs_world_t *world) {
    return ecs_has_pair(world, ecs_id(LettuceGameState),
        ecs_id(LettuceGameState),
        ecs_constant_to_entity(world, LettuceGameState, LettuceGameStatePlaying));
}

static LettuceGame* lettuceInputGame(ecs_world_t *world) {
    if (!lettucePlaying(world) || flecsEngine_uiMouseCaptured(world)) {
        return NULL;
    }
    return ecs_singleton_ensure(world, LettuceGame);
}

static void lettuceCancel(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    (void)argc;
    (void)argv;
    ecs_world_t *world = ctx->world;
    LettuceGame *game = lettucePlaying(world)
        ? ecs_singleton_ensure(world, LettuceGame) : NULL;
    if (!game || !(game->selected || game->shovel)) {
        lettuceResolve(future, false);
        return;
    }

    game->selected = 0;
    game->shovel = false;
    ecs_singleton_modified(world, LettuceGame);
    lettuceResolve(future, true);
}

static bool lettuceCollectSun(ecs_world_t *world, LettuceGame *game,
    const LettuceLawn *lawn)
{
    ecs_entity_t hit = 0;
    LettuceSunState *hit_sun = NULL;
    int32_t hit_value = 0;
    float lowest = 1e30f;

    ecs_iter_t sit = ecs_query_iter(world, game->sun_query);
    while (ecs_query_next(&sit)) {
        LettuceSunState *sun = ecs_field(&sit, LettuceSunState, 0);
        const LettuceSunDrop *drop = ecs_field(&sit, LettuceSunDrop, 1);
        const FlecsPosition3 *pos = ecs_field(&sit, FlecsPosition3, 2);

        for (int i = 0; i < sit.count; i ++) {
            if (sun[i].collect_time > 0 || pos[i].y >= lowest) {
                continue;
            }
            if (lettuceColAt(lawn, pos[i].x) != game->hover_col) {
                continue;
            }
            if (lettuceRowAt(lawn, pos[i].z) != game->hover_row) {
                continue;
            }

            hit = sit.entities[i];
            hit_sun = &sun[i];
            hit_value = drop->value;
            lowest = pos[i].y;
        }
    }

    if (!hit) {
        return false;
    }

    hit_sun->collect_time = 0.35f;
    game->sun += hit_value;

    const FlecsPosition3 *pos = ecs_get(world, hit, FlecsPosition3);
    lettuceBurst(world, game->glow_pool, game->pop_burst,
        pos->x, pos->y, pos->z);
    return true;
}

static bool lettucePlant(ecs_world_t *world, LettuceGame *game,
    const LettuceLawn *lawn)
{
    ecs_entity_t *cell = lettuceCell(game, game->hover_row, game->hover_col);
    const LettuceCost *cost = ecs_get(world, game->selected, LettuceCost);

    if (*cell || !cost || game->sun < cost->sun) {
        return false;
    }

    float x = lettuceTileX(lawn, game->hover_col);
    float z = lettuceTileZ(lawn, game->hover_row);

    ecs_entity_t plant = ecs_new_w_pair(world, EcsIsA, game->selected);
    ecs_add_pair(world, plant, EcsChildOf, game->plants);
    ecs_set(world, plant, FlecsPosition3, {x, 0, z});
    ecs_set(world, plant, FlecsRotation3, {0, 0, 0});
    ecs_set(world, plant, FlecsScale3, {1, 1, 1});

    const LettuceMaxHealth *max_health = ecs_get(
        world, game->selected, LettuceMaxHealth);

    ecs_set(world, plant, LettucePlant, {
        .row = game->hover_row,
        .col = game->hover_col
    });
    ecs_set(world, plant, LettuceHealth, {max_health->value});
    ecs_set(world, plant, LettuceAnim, {lettuceRandf() * 6.2832f});

    *cell = plant;

    game->sun -= cost->sun;
    game->planted ++;

    lettuceBurst(world, game->fx_pool, game->chomp_burst, x, 0.2f, z);
    return true;
}

static void lettuceUse(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    (void)argc;
    (void)argv;
    ecs_world_t *world = ctx->world;
    LettuceGame *game = lettuceInputGame(world);
    if (!game || game->hover_row < 0) {
        lettuceResolve(future, false);
        return;
    }

    LettuceLawn lawn = ecs_const_var_get_t(world, "cfg.lawn", LettuceLawn);
    bool changed = false;

    if (lettuceCollectSun(world, game, &lawn)) {
        changed = true;
    } else if (game->shovel) {
        if (lettuceDig(world, game, &lawn, game->hover_row, game->hover_col)) {
            game->shovel = false;
            changed = true;
        }
    } else if (game->selected) {
        changed = lettucePlant(world, game, &lawn);
    }

    if (changed) {
        ecs_singleton_modified(world, LettuceGame);
    }
    lettuceResolve(future, changed);
}

static void lettuceDigAction(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    (void)argc;
    (void)argv;
    ecs_world_t *world = ctx->world;
    LettuceGame *game = lettuceInputGame(world);
    if (!game) {
        lettuceResolve(future, false);
        return;
    }

    if (game->selected || game->shovel) {
        game->selected = 0;
        game->shovel = false;
        ecs_singleton_modified(world, LettuceGame);
        lettuceResolve(future, true);
        return;
    }

    if (game->hover_row < 0) {
        lettuceResolve(future, false);
        return;
    }

    LettuceLawn lawn = ecs_const_var_get_t(world, "cfg.lawn", LettuceLawn);
    lettuceResolve(future,
        lettuceDig(world, game, &lawn, game->hover_row, game->hover_col));
}

void LettuceHover(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    LettuceGame *game = ecs_field(it, LettuceGame, 0);
    const FlecsInput *input = ecs_field(it, FlecsInput, 1);

    LettuceLawn lawn = ecs_const_var_get_t(world, "cfg.lawn", LettuceLawn);

    game->hover_row = -1;
    game->hover_col = -1;

    vec3 origin, dir, ground;
    flecsEngine_cameraScreenRay(world, game->camera,
        input->mouse.view_norm.x, input->mouse.view_norm.y, origin, dir);

    if (flecsEngine_rayPlaneY(origin, dir, 0.0f, ground)) {
        int32_t col = lettuceColAt(&lawn, ground[0]);
        int32_t row = lettuceRowAt(&lawn, ground[2]);
        if (lettuceOnLawn(row, col)) {
            game->hover_row = row;
            game->hover_col = col;
        }
    }
}

void LettuceHoverGhost(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    const LettuceGame *game = ecs_field(it, LettuceGame, 0);

    if (game->hover_row < 0 || (!game->selected && !game->shovel)) {
        return;
    }

    LettuceLawn lawn = ecs_const_var_get_t(world, "cfg.lawn", LettuceLawn);
    float x = lettuceTileX(&lawn, game->hover_col);
    float z = lettuceTileZ(&lawn, game->hover_row);

    bool free_cell = !game->grid[game->hover_row * LETTUCE_COLS +
        game->hover_col];

    if (game->shovel && free_cell) {
        return;
    }

    flecsEngine_draw(world, game->marker_prefab,
        &(flecs_draw_instance_t){
            .position = {x, 0.12f, z},
            .scale = {1, 1, 1}
        }, 1);

    if (game->shovel) {
        return;
    }

    const LettuceCost *cost = ecs_get(world, game->selected, LettuceCost);
    if (!cost) {
        return;
    }

    if (free_cell && game->sun >= cost->sun) {
        float s = ecs_const_var_get_t(world, "cfg.plantScale", ecs_f32_t) * 0.7f;

        flecsEngine_draw(world, game->selected,
            &(flecs_draw_instance_t){
                .position = {x, 0.1f, z},
                .scale = {s, s, s}
            }, 1);
    }
}

void LettuceAnimIncrement(ecs_iter_t *it) {
    LettuceAnim *anim = ecs_field(it, LettuceAnim, 0);
    const LettuceAnimSpeed *speed = ecs_field(it, LettuceAnimSpeed, 1);

    for (int i = 0; i < it->count; i ++) {
        anim[i].value += speed->value * it->delta_time;
    }
}

void LettuceSuns(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    LettuceSunState *sun = ecs_field(it, LettuceSunState, 0);
    const LettuceSunDrop *drop = ecs_field(it, LettuceSunDrop, 1);
    FlecsPosition3 *pos = ecs_field(it, FlecsPosition3, 2);
    FlecsRotation3 *rot = ecs_field(it, FlecsRotation3, 3);
    FlecsScale3 *scale = ecs_field(it, FlecsScale3, 4);

    for (int i = 0; i < it->count; i ++) {
        rot[i].y += it->delta_time * 1.4f;

        if (sun[i].collect_time > 0) {
            sun[i].collect_time -= it->delta_time;
            if (sun[i].collect_time <= 0) {
                ecs_delete(world, it->entities[i]);
                continue;
            }

            float t = sun[i].collect_time / 0.35f;
            pos[i].y += it->delta_time * 6.0f;
            scale[i].x = scale[i].y = scale[i].z = t;
            continue;
        }

        if (pos[i].y > drop->land_y) {
            pos[i].y -= drop->fall_speed * it->delta_time;
            if (pos[i].y < drop->land_y) {
                pos[i].y = drop->land_y;
            }
        }

        sun[i].life -= it->delta_time;
        if (sun[i].life <= 0) {
            ecs_delete(world, it->entities[i]);
            continue;
        }

        float s = 1.0f;
        if (sun[i].life < 2.0f) {
            s = 0.55f + 0.45f * (sun[i].life * 0.5f);
        }
        scale[i].x = scale[i].y = scale[i].z = s;
    }
}

void LettucePlants(ecs_iter_t *it) {
    const LettuceHealth *health = ecs_field(it, LettuceHealth, 0);
    const LettuceMaxHealth *max_health = ecs_field(it, LettuceMaxHealth, 1);
    const LettuceAnim *anim = ecs_field(it, LettuceAnim, 2);
    FlecsScale3 *scale = ecs_field(it, FlecsScale3, 3);
    FlecsRotation3 *rot = ecs_field(it, FlecsRotation3, 4);

    float base = ecs_const_var_get_t(it->world, "cfg.plantScale", ecs_f32_t);

    for (int i = 0; i < it->count; i ++) {
        float t = health[i].value / max_health->value;
        if (t < 0) {
            t = 0;
        }

        float s = base * (0.78f + 0.22f * t);
        float bob = sinf(anim[i].value) * 0.035f;

        scale[i].x = s * (1.0f - bob * 0.5f);
        scale[i].y = s * (1.0f + bob);
        scale[i].z = s * (1.0f - bob * 0.5f);
        rot[i].z = sinf(anim[i].value * 0.63f) * 0.05f;
    }
}

void LettuceProduce(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    LettuceActionTimer *timer = ecs_field(it, LettuceActionTimer, 0);
    const LettuceActionInterval *interval =
        ecs_field(it, LettuceActionInterval, 1);
    const FlecsPosition3 *pos = ecs_field(it, FlecsPosition3, 2);
    const LettuceGame *game = ecs_field(it, LettuceGame, 3);

    for (int i = 0; i < it->count; i ++) {
        timer[i].value -= it->delta_time;
        if (timer[i].value > 0) {
            continue;
        }

        timer[i].value = interval->value;

        float dx = (lettuceRandf() - 0.5f) * 1.1f;
        float dz = (lettuceRandf() - 0.5f) * 1.1f;

        lettuceSpawnSun(world, game, pos[i].x + dx, pos[i].y + 1.6f,
            pos[i].z + dz);

        lettuceBurst(world, game->glow_pool, game->pop_burst,
            pos[i].x, pos[i].y + 1.4f, pos[i].z);
    }
}

void LettuceShoot(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    const LettuceShooter *shooter = ecs_field(it, LettuceShooter, 0);
    LettuceActionTimer *timer = ecs_field(it, LettuceActionTimer, 1);
    const LettuceActionInterval *interval =
        ecs_field(it, LettuceActionInterval, 2);
    const LettuceDamage *damage = ecs_field(it, LettuceDamage, 3);
    const LettuceSpeed *speed = ecs_field(it, LettuceSpeed, 4);
    const LettucePlant *plant = ecs_field(it, LettucePlant, 5);
    const FlecsPosition3 *pos = ecs_field(it, FlecsPosition3, 6);
    const LettuceGame *game = ecs_field(it, LettuceGame, 7);

    for (int i = 0; i < it->count; i ++) {
        if (timer[i].value > 0) {
            timer[i].value -= it->delta_time;
        }

        int32_t row = plant[i].row;

        if (game->row_max[row] < pos[i].x + 0.4f) {
            continue;
        }

        if (timer[i].value > 0) {
            continue;
        }

        timer[i].value = interval->value;

        float x = pos[i].x + 0.55f;
        float y = shooter->height;
        float z = pos[i].z;

        ecs_entity_t pea = ecs_new_w_pair(world, EcsIsA, game->pea_prefab);
        ecs_add_pair(world, pea, EcsChildOf, game->peas);
        ecs_set(world, pea, FlecsPosition3, {x, y, z});
        ecs_set(world, pea, LettucePea, { .row = row });
        ecs_set(world, pea, LettuceSpeed, { speed->value });
        ecs_set(world, pea, LettuceDamage, { .damage = damage->damage });
    }
}

void LettucePeas(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    const LettucePea *pea = ecs_field(it, LettucePea, 0);
    const LettuceSpeed *speed = ecs_field(it, LettuceSpeed, 1);
    const LettuceDamage *damage = ecs_field(it, LettuceDamage, 2);
    FlecsPosition3 *pos = ecs_field(it, FlecsPosition3, 3);
    LettuceGame *game = ecs_field(it, LettuceGame, 4);

    float despawn_x = ecs_const_var_get_t(world, "cfg.despawnX", ecs_f32_t);

    for (int i = 0; i < it->count; i ++) {
        pos[i].x += speed[i].value * it->delta_time;

        if (pos[i].x > despawn_x) {
            ecs_delete(world, it->entities[i]);
            continue;
        }

        bool hit = false;
        ecs_iter_t zit = ecs_query_iter(world, game->zombie_query);

        while (!hit && ecs_query_next(&zit)) {
            LettuceZombie *z = ecs_field(&zit, LettuceZombie, 0);
            LettuceHealth *health = ecs_field(&zit, LettuceHealth, 1);
            const FlecsPosition3 *zpos = ecs_field(&zit, FlecsPosition3, 2);
            LettuceArmor *armor = ecs_field_is_set(&zit, 3) ?
                ecs_field(&zit, LettuceArmor, 3) : NULL;

            for (int j = 0; j < zit.count; j ++) {
                if (health[j].value <= 0 || z[j].row != pea[i].row) {
                    continue;
                }
                if (fabsf(zpos[j].x - pos[i].x) > 0.6f) {
                    continue;
                }

                lettuceDamageZombie(world, game, zit.entities[j], &z[j],
                    &health[j], armor ? &armor[j] : NULL, &zpos[j],
                    damage[i].damage);

                lettuceBurst(world, game->fx_pool, game->splat_burst,
                    pos[i].x, pos[i].y, pos[i].z);

                ecs_delete(world, it->entities[i]);
                hit = true;
                break;
            }
        }

        if (hit) {
            ecs_iter_fini(&zit);
        }
    }
}

void LettuceZombies(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    LettuceZombie *zombie = ecs_field(it, LettuceZombie, 0);
    const LettuceHealth *health = ecs_field(it, LettuceHealth, 1);
    const LettuceSpeed *speed = ecs_field(it, LettuceSpeed, 2);
    const LettuceDamage *damage = ecs_field(it, LettuceDamage, 3);
    const LettuceAnim *anim = ecs_field(it, LettuceAnim, 4);
    const LettuceAnimSpeed *phase_speed = ecs_field(it, LettuceAnimSpeed, 5);
    FlecsPosition3 *pos = ecs_field(it, FlecsPosition3, 6);
    FlecsRotation3 *rot = ecs_field(it, FlecsRotation3, 7);
    LettuceGame *game = ecs_field(it, LettuceGame, 8);

    LettuceLawn lawn = ecs_const_var_get_t(world, "cfg.lawn", LettuceLawn);
    float eat_range = ecs_const_var_get_t(world, "cfg.eatRange", ecs_f32_t);
    float lawn_edge = lettuceTileX(&lawn, LETTUCE_COLS - 1) + lawn.tile * 0.5f;

    for (int i = 0; i < it->count; i ++) {
        if (health[i].value <= 0) {
            continue;
        }

        float walk_speed = speed->value;
        float dps = damage->dps;

        int32_t row = zombie[i].row;
        if (pos[i].x <= lawn_edge && pos[i].x > game->row_max[row]) {
            game->row_max[row] = pos[i].x;
        }
        if (pos[i].x < game->row_min[row]) {
            game->row_min[row] = pos[i].x;
        }

        ecs_entity_t target = 0;
        float target_x = 0;

        int32_t col = lettuceColAt(&lawn, pos[i].x);
        if (col > LETTUCE_COLS - 1) {
            col = LETTUCE_COLS - 1;
        }

        for (int c = col; c >= 0; c --) {
            ecs_entity_t plant = game->grid[row * LETTUCE_COLS + c];
            if (plant) {
                target = plant;
                target_x = lettuceTileX(&lawn, c);
                break;
            }
        }

        const LettucePlant *plant = NULL;
        if (target && (pos[i].x - target_x) <= eat_range) {
            plant = ecs_get(world, target, LettucePlant);
        }

        if (plant) {
            LettuceHealth *plant_health = ecs_get_mut(
                world, target, LettuceHealth);

            float before = anim[i].value - it->delta_time * phase_speed->value;
            zombie[i].eating = target;

            plant_health->value -= dps * it->delta_time;

            if (floorf(before) != floorf(anim[i].value)) {
                lettuceBurst(world, game->fx_pool, game->chomp_burst,
                    target_x + 0.5f, 0.8f, pos[i].z);
            }

            if (plant_health->value <= 0) {
                ecs_delete(world, target);
                zombie[i].eating = 0;
            }

            rot[i].z = sinf(anim[i].value * 3.0f) * 0.05f;
            pos[i].y = 0;
        } else {
            zombie[i].eating = 0;
            pos[i].x -= walk_speed * it->delta_time;

            rot[i].z = sinf(anim[i].value) * 0.08f;
            rot[i].y = sinf(anim[i].value * 0.5f) * 0.12f;
            pos[i].y = fabsf(sinf(anim[i].value)) * 0.07f;
        }
    }
}

void LettuceLimbs(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    const LettuceLimb *limb = ecs_field(it, LettuceLimb, 0);
    const LettuceAnim *anim = ecs_field(it, LettuceAnim, 1);
    FlecsRotation3 *rot = ecs_field(it, FlecsRotation3, 2);

    for (int i = 0; i < it->count; i ++) {
        ecs_entity_t parent = ecs_get_parent(world, it->entities[i]);
        const LettuceZombie *z = ecs_get(world, parent, LettuceZombie);
        if (!z) {
            continue;
        }

        const LettuceAnim *zanim = ecs_get(world, parent, LettuceAnim);
        if (!zanim) {
            continue;
        }

        float swing = z->eating ? 0.25f : 1.0f;
        rot[i].z = sinf(zanim->value + anim[i].value) * limb->amount * swing +
            limb->bias;
    }
}

void LettuceCorpses(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    LettuceCorpse *corpse = ecs_field(it, LettuceCorpse, 0);
    FlecsPosition3 *pos = ecs_field(it, FlecsPosition3, 1);
    FlecsRotation3 *rot = ecs_field(it, FlecsRotation3, 2);

    for (int i = 0; i < it->count; i ++) {
        corpse[i].time += it->delta_time;

        float t = corpse[i].time / 1.1f;
        if (t > 1.0f) {
            t = 1.0f;
        }

        rot[i].z = -t * 1.5708f;
        rot[i].y = 0;
        pos[i].y = -t * t * 0.9f;

        if (corpse[i].time > 1.6f) {
            ecs_delete(world, it->entities[i]);
        }
    }
}

void LettuceMowers(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    LettuceMower *mower = ecs_field(it, LettuceMower, 0);
    const LettuceSpeed *speed = ecs_field(it, LettuceSpeed, 1);
    FlecsPosition3 *pos = ecs_field(it, FlecsPosition3, 2);
    FlecsRotation3 *rot = ecs_field(it, FlecsRotation3, 3);
    LettuceGame *game = ecs_field(it, LettuceGame, 4);

    float trigger_x = ecs_const_var_get_t(world, "cfg.mowerTriggerX", ecs_f32_t);
    float despawn_x = ecs_const_var_get_t(world, "cfg.despawnX", ecs_f32_t);

    for (int i = 0; i < it->count; i ++) {
        int32_t row = mower[i].row;
        if (row < 0) {
            continue;
        }

        if (!mower[i].running) {
            if (game->row_min[row] <= trigger_x) {
                mower[i].running = true;
                lettuceBurst(world, game->fx_pool, game->chomp_burst,
                    pos[i].x, 0.4f, pos[i].z);
            }
            continue;
        }

        pos[i].x += speed->value * it->delta_time;
        rot[i].x -= it->delta_time * 22.0f;

        ecs_iter_t zit = ecs_query_iter(world, game->zombie_query);
        while (ecs_query_next(&zit)) {
            LettuceZombie *z = ecs_field(&zit, LettuceZombie, 0);
            LettuceHealth *health = ecs_field(&zit, LettuceHealth, 1);
            const FlecsPosition3 *zpos = ecs_field(&zit, FlecsPosition3, 2);

            for (int j = 0; j < zit.count; j ++) {
                if (health[j].value <= 0 || z[j].row != row) {
                    continue;
                }
                if (fabsf(zpos[j].x - pos[i].x) > 0.9f) {
                    continue;
                }

                lettuceKillZombie(world, game, zit.entities[j], &z[j],
                    &health[j], &zpos[j]);
            }
        }

        if (pos[i].x > despawn_x) {
            mower[i].running = false;
            mower[i].row = -1;
            pos[i].y = -30.0f;
        }
    }
}

void LettuceBombs(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    const LettuceBomb *bomb = ecs_field(it, LettuceBomb, 0);
    LettuceActionTimer *timer = ecs_field(it, LettuceActionTimer, 1);
    const LettuceDamage *damage = ecs_field(it, LettuceDamage, 2);
    const FlecsPosition3 *pos = ecs_field(it, FlecsPosition3, 4);
    FlecsScale3 *scale = ecs_field(it, FlecsScale3, 5);
    LettuceGame *game = ecs_field(it, LettuceGame, 6);

    for (int i = 0; i < it->count; i ++) {
        timer[i].value -= it->delta_time;

        if (timer[i].value > 0) {
            float pulse = 1.0f + 0.22f * sinf(timer[i].value * 22.0f);
            scale[i].x *= pulse;
            scale[i].y *= pulse;
            scale[i].z *= pulse;
            continue;
        }

        float radius = bomb->radius;
        float dmg = damage->damage;

        lettuceBurst(world, game->glow_pool, game->boom_burst,
            pos[i].x, 0.8f, pos[i].z);

        ecs_iter_t zit = ecs_query_iter(world, game->zombie_query);
        while (ecs_query_next(&zit)) {
            LettuceZombie *z = ecs_field(&zit, LettuceZombie, 0);
            LettuceHealth *health = ecs_field(&zit, LettuceHealth, 1);
            const FlecsPosition3 *zpos = ecs_field(&zit, FlecsPosition3, 2);
            LettuceArmor *armor = ecs_field_is_set(&zit, 3) ?
                ecs_field(&zit, LettuceArmor, 3) : NULL;

            for (int j = 0; j < zit.count; j ++) {
                if (health[j].value <= 0) {
                    continue;
                }
                if (fabsf(zpos[j].x - pos[i].x) > radius) {
                    continue;
                }
                if (fabsf(zpos[j].z - pos[i].z) > 2.4f) {
                    continue;
                }

                if (armor) {
                    armor[j].value = 0;
                }
                lettuceDamageZombie(world, game, zit.entities[j], &z[j],
                    &health[j], armor ? &armor[j] : NULL, &zpos[j], dmg);
            }
        }

        ecs_delete(world, it->entities[i]);
    }
}

void LettuceWaves(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    LettuceGame *game = ecs_field(it, LettuceGame, 0);

    LettuceLawn lawn = ecs_const_var_get_t(world, "cfg.lawn", LettuceLawn);
    bool changed = false;

    int32_t alive = 0;
    ecs_iter_t zit = ecs_query_iter(world, game->zombie_query);
    while (ecs_query_next(&zit)) {
        const LettuceHealth *health = ecs_field(&zit, LettuceHealth, 1);
        for (int i = 0; i < zit.count; i ++) {
            if (health[i].value > 0) {
                alive ++;
            }
        }
    }

    if (alive != game->alive) {
        game->alive = alive;
        changed = true;
    }

    game->sun_timer -= it->delta_time;
    if (game->sun_timer <= 0) {
        game->sun_timer = ecs_const_var_get_t(
            world, "cfg.sunSkyInterval", ecs_f32_t) *
                (0.75f + lettuceRandf() * 0.5f);

        int32_t col = (int32_t)(lettuceRandf() * (LETTUCE_COLS - 0.001f));
        int32_t row = (int32_t)(lettuceRandf() * (LETTUCE_ROWS - 0.001f));

        lettuceSpawnSun(world, game,
            lettuceTileX(&lawn, col) + (lettuceRandf() - 0.5f),
            ecs_const_var_get_t(world, "cfg.sunSkyY", ecs_f32_t),
            lettuceTileZ(&lawn, row) + (lettuceRandf() - 0.5f));
    }

    if (game->spawn_left > 0) {
        game->spawn_timer -= it->delta_time;

        if (game->spawn_timer <= 0) {
            float interval = ecs_const_var_get_t(
                world, "cfg.spawnInterval", ecs_f32_t);
            interval -= (float)game->wave * 0.11f;
            if (interval < 0.9f) {
                interval = 0.9f;
            }

            game->spawn_timer = interval * (0.7f + lettuceRandf() * 0.6f);
            game->spawn_left --;
            changed = true;

            ecs_entity_t prefab = game->zombie_prefab;
            float roll = lettuceRandf();

            if (game->wave >= ecs_const_var_get_t(
                    world, "cfg.bucketWave", ecs_i32_t) && roll < 0.26f)
            {
                prefab = game->bucket_prefab;
            } else if (game->wave >= ecs_const_var_get_t(
                    world, "cfg.coneWave", ecs_i32_t) && roll < 0.6f)
            {
                prefab = game->cone_prefab;
            }

            int32_t row = (int32_t)(lettuceRandf() * (LETTUCE_ROWS - 0.001f));

            ecs_entity_t zombie = ecs_new_w_pair(world, EcsIsA, prefab);
            ecs_add_pair(world, zombie, EcsChildOf, game->zombies);
            ecs_set(world, zombie, FlecsPosition3, {
                ecs_const_var_get_t(world, "cfg.spawnX", ecs_f32_t) +
                    lettuceRandf() * 2.0f,
                0,
                lettuceTileZ(&lawn, row)
            });
            ecs_set(world, zombie, FlecsRotation3, {0, 0, 0});
            ecs_set(world, zombie, FlecsScale3, {1, 1, 1});

            const LettuceMaxHealth *max_health = ecs_get(
                world, prefab, LettuceMaxHealth);
            const LettuceMaxArmor *max_armor = ecs_get(
                world, prefab, LettuceMaxArmor);

            ecs_set(world, zombie, LettuceZombie, { .row = row });
            ecs_set(world, zombie, LettuceHealth, {max_health->value});
            if (max_armor) {
                ecs_set(world, zombie, LettuceArmor, {max_armor->value});
            }
            ecs_set(world, zombie, LettuceAnim, {lettuceRandf() * 6.2832f});
        }
    } else if (game->wave < game->waves) {
        if (alive == 0 && game->wave > 0 && game->wave_timer > 6.0f) {
            game->wave_timer = 6.0f;
        }

        game->wave_timer -= it->delta_time;

        if (game->wave_timer <= 0) {
            game->wave ++;
            game->spawn_left = ecs_const_var_get_t(
                world, "cfg.zombieBase", ecs_i32_t) + (game->wave - 1) *
                    ecs_const_var_get_t(world, "cfg.zombieStep", ecs_i32_t);
            game->spawn_timer = 1.5f;
            game->wave_timer = ecs_const_var_get_t(
                world, "cfg.waveGap", ecs_f32_t);
            changed = true;
        }
    } else if (alive == 0) {
        lettuceSetState(world, LettuceGameStateWon);
        changed = true;
    }

    float lose_x = ecs_const_var_get_t(world, "cfg.loseX", ecs_f32_t);
    for (int i = 0; i < LETTUCE_ROWS; i ++) {
        if (game->row_min[i] <= lose_x) {
            lettuceSetState(world, LettuceGameStateLost);
            changed = true;
            break;
        }
    }

    if (changed) {
        ecs_singleton_modified(world, LettuceGame);
    }
}

static void lettuceRestart(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    (void)argc;
    (void)argv;
    ecs_world_t *world = ctx->world;
    if (lettucePlaying(world)) {
        lettuceResolve(future, false);
        return;
    }

    LettuceGame *game = ecs_singleton_ensure(world, LettuceGame);

    ecs_delete_with(world, ecs_pair(EcsChildOf, game->plants));
    ecs_delete_with(world, ecs_pair(EcsChildOf, game->zombies));
    ecs_delete_with(world, ecs_pair(EcsChildOf, game->peas));
    ecs_delete_with(world, ecs_pair(EcsChildOf, game->suns));

    memset(game->grid, 0, sizeof(game->grid));

    if (!ecs_script(world, { .filename = "etc/scene.flecs" })) {
        ecs_err("failed to reload etc/scene.flecs");
    }
    lettuceResolve(future, true);
}

void Lettuce_prayImport(ecs_world_t *world) {
    ECS_MODULE(world, Lettuce_pray);

    ECS_META_COMPONENT(world, LettuceGameState);
    ECS_META_COMPONENT(world, LettuceLawn);
    ECS_META_COMPONENT(world, LettuceGame);
    ECS_META_COMPONENT(world, LettuceCost);
    ECS_META_COMPONENT(world, LettuceSeedPacket);

    ecs_add_pair(world, ecs_id(LettuceSeedPacket), EcsWith,
        ecs_id(FlecsUiWidgetState));
    ECS_META_COMPONENT(world, LettuceMaxHealth);
    ECS_META_COMPONENT(world, LettuceMaxArmor);
    ECS_META_COMPONENT(world, LettuceHealth);
    ECS_META_COMPONENT(world, LettuceArmor);
    ECS_META_COMPONENT(world, LettuceDamage);
    ECS_META_COMPONENT(world, LettuceSpeed);
    ECS_META_COMPONENT(world, LettuceAnim);
    ECS_META_COMPONENT(world, LettuceAnimSpeed);
    ECS_META_COMPONENT(world, LettuceActionInterval);
    ECS_META_COMPONENT(world, LettuceActionTimer);
    ECS_META_COMPONENT(world, LettucePlant);
    ECS_META_COMPONENT(world, LettuceShooter);
    ECS_META_COMPONENT(world, LettuceBomb);
    ECS_META_COMPONENT(world, LettuceZombie);
    ECS_META_COMPONENT(world, LettuceLimb);
    ECS_META_COMPONENT(world, LettuceCorpse);
    ECS_META_COMPONENT(world, LettucePea);
    ECS_META_COMPONENT(world, LettuceSunDrop);
    ECS_META_COMPONENT(world, LettuceSunState);
    ECS_META_COMPONENT(world, LettuceMower);

    ECS_TAG_DEFINE(world, LettuceProducer);

    ecs_add_pair(world, ecs_id(LettuceCost), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(LettuceSunDrop), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(LettuceMaxHealth), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(LettuceMaxArmor), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(LettuceDamage), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(LettuceSpeed), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(LettuceAnimSpeed), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(LettuceActionInterval), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(LettuceShooter), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(LettuceBomb), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(LettuceLimb), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, LettuceProducer, EcsOnInstantiate, EcsInherit);

    ecs_add_id(world, ecs_id(LettuceGameState), EcsExclusive);
    ecs_add_id(world, ecs_id(LettuceGameState), EcsSingleton);
    ecs_add_id(world, ecs_id(LettuceGame), EcsSingleton);

    ecs_async_function(world, {
        .name = "useAtCursor",
        .parent = ecs_id(Lettuce_pray),
        .return_type = ecs_id(ecs_bool_t),
        .callback = lettuceUse
    });

    ecs_async_function(world, {
        .name = "digAtCursor",
        .parent = ecs_id(Lettuce_pray),
        .return_type = ecs_id(ecs_bool_t),
        .callback = lettuceDigAction
    });

    ecs_async_function(world, {
        .name = "clearSelection",
        .parent = ecs_id(Lettuce_pray),
        .return_type = ecs_id(ecs_bool_t),
        .callback = lettuceCancel
    });

    ecs_async_function(world, {
        .name = "restart",
        .parent = ecs_id(Lettuce_pray),
        .return_type = ecs_id(ecs_bool_t),
        .callback = lettuceRestart
    });

    ecs_add_pair(world, ecs_id(LettuceSpeed), EcsWith, FlecsDynamicTransform);
    ecs_add_pair(world, ecs_id(LettuceAnim), EcsWith, FlecsDynamicTransform);

    LettuceGame *game = ecs_singleton_ensure(world, LettuceGame);

    game->zombie_query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "LettuceZombieQuery" }),
        .terms = {
            { .id = ecs_id(LettuceZombie) },
            { .id = ecs_id(LettuceHealth) },
            { .id = ecs_id(FlecsPosition3) },
            { .id = ecs_id(LettuceArmor), .oper = EcsOptional }
        }
    });

    game->sun_query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "LettuceSunQuery" }),
        .terms = {
            { .id = ecs_id(LettuceSunState) },
            { .id = ecs_id(LettuceSunDrop), .inout = EcsIn },
            { .id = ecs_id(FlecsPosition3) }
        }
    });

    ecs_singleton_modified(world, LettuceGame);

    ecs_entity_t playing = ecs_constant_to_entity(
        world, LettuceGameState, LettuceGameStatePlaying);

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "LettucePlantOnRemove" }),
        .query.terms = {
            { .id = ecs_id(LettucePlant) },
            { .id = ecs_id(LettuceGame) }
        },
        .events = { EcsOnRemove },
        .callback = LettucePlantOnRemove
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "LettuceUiClick" }),
        .query.terms = {
            { .id = ecs_id(LettuceSeedPacket) }
        },
        .events = { FlecsUiClicked },
        .callback = LettuceUiClick
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "LettuceResetGrid" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(LettuceGame) }
        },
        .callback = LettuceResetGrid
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "LettuceHover" }),
        .phase = EcsOnLoad,
        .query.terms = {
            { .id = ecs_id(LettuceGame) },
            { .id = ecs_id(FlecsInput) },
            { .id = ecs_pair_t(LettuceGameState, playing) }
        },
        .callback = LettuceHover
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "LettuceHoverGhost" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(LettuceGame) },
            { .id = ecs_pair_t(LettuceGameState, playing) }
        },
        .callback = LettuceHoverGhost
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "LettuceAnimIncrement" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(LettuceAnim) },
            { .id = ecs_id(LettuceAnimSpeed) }
        },
        .callback = LettuceAnimIncrement
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "LettuceSuns" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(LettuceSunState) },
            { .id = ecs_id(LettuceSunDrop), .inout = EcsIn },
            { .id = ecs_id(FlecsPosition3) },
            { .id = ecs_id(FlecsRotation3) },
            { .id = ecs_id(FlecsScale3) }
        },
        .callback = LettuceSuns
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "LettucePlants" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(LettuceHealth) },
            { .id = ecs_id(LettuceMaxHealth) },
            { .id = ecs_id(LettuceAnim) },
            { .id = ecs_id(FlecsScale3) },
            { .id = ecs_id(FlecsRotation3) },
            { .id = ecs_id(LettucePlant), .inout = EcsInOutNone }
        },
        .callback = LettucePlants
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "LettuceProduce" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(LettuceActionTimer) },
            { .id = ecs_id(LettuceActionInterval) },
            { .id = ecs_id(FlecsPosition3) },
            { .id = ecs_id(LettuceGame) },
            { .id = LettuceProducer },
            { .id = ecs_pair_t(LettuceGameState, playing) }
        },
        .callback = LettuceProduce
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "LettuceZombies" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(LettuceZombie) },
            { .id = ecs_id(LettuceHealth) },
            { .id = ecs_id(LettuceSpeed) },
            { .id = ecs_id(LettuceDamage) },
            { .id = ecs_id(LettuceAnim) },
            { .id = ecs_id(LettuceAnimSpeed) },
            { .id = ecs_id(FlecsPosition3) },
            { .id = ecs_id(FlecsRotation3) },
            { .id = ecs_id(LettuceGame) },
            { .id = ecs_pair_t(LettuceGameState, playing) }
        },
        .callback = LettuceZombies
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "LettuceShoot" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(LettuceShooter) },
            { .id = ecs_id(LettuceActionTimer) },
            { .id = ecs_id(LettuceActionInterval) },
            { .id = ecs_id(LettuceDamage) },
            { .id = ecs_id(LettuceSpeed) },
            { .id = ecs_id(LettucePlant) },
            { .id = ecs_id(FlecsPosition3) },
            { .id = ecs_id(LettuceGame) },
            { .id = ecs_pair_t(LettuceGameState, playing) }
        },
        .callback = LettuceShoot
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "LettucePeas" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(LettucePea) },
            { .id = ecs_id(LettuceSpeed) },
            { .id = ecs_id(LettuceDamage) },
            { .id = ecs_id(FlecsPosition3) },
            { .id = ecs_id(LettuceGame) },
            { .id = ecs_pair_t(LettuceGameState, playing) }
        },
        .callback = LettucePeas
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "LettuceLimbs" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(LettuceLimb) },
            { .id = ecs_id(LettuceAnim) },
            { .id = ecs_id(FlecsRotation3) }
        },
        .callback = LettuceLimbs
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "LettuceCorpses" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(LettuceCorpse) },
            { .id = ecs_id(FlecsPosition3) },
            { .id = ecs_id(FlecsRotation3) }
        },
        .callback = LettuceCorpses
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "LettuceMowers" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(LettuceMower) },
            { .id = ecs_id(LettuceSpeed) },
            { .id = ecs_id(FlecsPosition3) },
            { .id = ecs_id(FlecsRotation3) },
            { .id = ecs_id(LettuceGame) },
            { .id = ecs_pair_t(LettuceGameState, playing) }
        },
        .callback = LettuceMowers
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "LettuceBombs" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(LettuceBomb) },
            { .id = ecs_id(LettuceActionTimer) },
            { .id = ecs_id(LettuceDamage) },
            { .id = ecs_id(LettucePlant), .inout = EcsInOutNone },
            { .id = ecs_id(FlecsPosition3) },
            { .id = ecs_id(FlecsScale3) },
            { .id = ecs_id(LettuceGame) },
            { .id = ecs_pair_t(LettuceGameState, playing) }
        },
        .callback = LettuceBombs
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "LettuceWaves" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(LettuceGame) },
            { .id = ecs_pair_t(LettuceGameState, playing) }
        },
        .callback = LettuceWaves
    });

}
