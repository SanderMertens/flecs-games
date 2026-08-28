#define ORE_ELSE_GAME_IMPL
#include "ore_else.h"

#include <time.h>

#include <stdio.h>
#include <stdlib.h>

ECS_TAG_DECLARE(OreScrap);

float oreRandf(void) {
    return (float)rand() / (float)RAND_MAX;
}

float oreTileX(const OreMap *map, int32_t col) {
    return map->x0 + (float)col * map->tile;
}

float oreTileZ(const OreMap *map, int32_t row) {
    return map->z0 + (float)row * map->tile;
}

int32_t oreColAt(const OreMap *map, float x) {
    return (int32_t)floorf((x - map->x0) / map->tile + 0.5f);
}

int32_t oreRowAt(const OreMap *map, float z) {
    return (int32_t)floorf((z - map->z0) / map->tile + 0.5f);
}

bool oreOnGrid(int32_t row, int32_t col) {
    return row >= 0 && row < ORE_ROWS && col >= 0 && col < ORE_COLS;
}

ecs_entity_t* oreDepositCell(OreGame *game, int32_t row, int32_t col) {
    return &game->deposit_grid[row * ORE_COLS + col];
}

void oreBurst(ecs_world_t *world, ecs_entity_t pool, ecs_entity_t burst,
    float x, float y, float z)
{
    if (!pool || !burst) {
        return;
    }

    const FlecsParticleBurst *b = ecs_get(world, burst, FlecsParticleBurst);
    if (!b) {
        return;
    }

    float at[3] = {x, y, z};
    flecsEngine_particlesBurst(world, pool, at, b);
}

typedef struct {
    ecs_entity_t pool;
    float size;
    float shape;
    float lift;
    float glow_lift;
    float peak;
    float range;
    float fade;
    FlecsRgba color;
} ore_blast_t;

static void oreBlast(ecs_world_t *world, ecs_entity_t root,
    float x, float y, float z, const ore_blast_t *blast)
{
    if (!root) {
        return;
    }

    float s = blast->size;
    float r = blast->shape;
    float at[3] = {x, y + blast->lift, z};

    ecs_iter_t it = ecs_children(world, root);
    while (ecs_children_next(&it)) {
        for (int i = 0; i < it.count; i ++) {
            const FlecsParticleBurst *b = ecs_get(
                world, it.entities[i], FlecsParticleBurst);
            if (!b) {
                continue;
            }

            FlecsParticleBurst p = *b;
            p.count = (int32_t)((float)b->count * s + 0.5f);
            if (p.count < 1) {
                p.count = 1;
            }
            p.size *= s;
            p.size_variance *= s;
            p.grow *= s;
            p.radial *= s;
            p.velocity.x *= s;
            p.velocity.y *= s;
            p.velocity.z *= s;
            p.velocity_variance.x *= s;
            p.velocity_variance.y *= s;
            p.velocity_variance.z *= s;
            p.shape_radius *= r;
            p.shape_thickness *= r;
            p.offset.x *= r;
            p.offset.y *= r;
            p.offset.z *= r;

            flecsEngine_particlesBurst(world,
                b->pool ? b->pool : blast->pool, at, &p);
        }
    }

    ecs_entity_t flash = ecs_new(world);
    ecs_set(world, flash, FlecsPosition3, {x, y + blast->glow_lift, z});
    ecs_set_ptr(world, flash, FlecsRgba, &blast->color);
    ecs_set(world, flash, FlecsPointLight, {
        .intensity = blast->peak,
        .range = blast->range
    });
    ecs_set(world, flash, OreFlashLight, {
        .peak = blast->peak,
        .fade = blast->fade,
        .level = blast->peak,
        .once = true
    });
}

void oreExplosion(ecs_world_t *world, OreGame *game, ecs_entity_t burst,
    float x, float y, float z, float scale)
{
    if (scale <= 0) {
        scale = 1;
    }

    oreBlast(world, burst, x, y, z, &(ore_blast_t){
        .pool = game->glow_pool,
        .size = scale,
        .shape = scale,
        .glow_lift = 0.45f * scale,
        .peak = 9 * scale,
        .range = 13 * scale,
        .fade = 9 * scale * 4.5f,
        .color = {255, 190, 118, 255}
    });
}

void oreCombust(ecs_world_t *world, OreGame *game, ecs_entity_t root,
    float x, float y, float z, float r)
{
    if (r <= 0.1f) {
        r = 0.1f;
    }

    if (r > 4) {
        r = 4;
    }

    float s = sqrtf(r);

    oreBlast(world, root, x, y, z, &(ore_blast_t){
        .pool = game->fx_pool,
        .size = s,
        .shape = r,
        .lift = r * 0.9f,
        .glow_lift = r * 0.9f,
        .peak = 4.5f * s,
        .range = 3 + 4 * r,
        .fade = 4.5f * s * 7,
        .color = {168, 255, 116, 255}
    });
}

bool oreCellAt(
    ecs_world_t *world,
    const OreGame *game,
    float nx,
    float ny,
    OreCell *out)
{
    OreMap map = ecs_const_var_get_t(world, "cfg.map", OreMap);

    vec3 origin, dir, ground;
    flecsEngine_cameraScreenRay(world, game->camera, nx, ny, origin, dir);

    if (!flecsEngine_rayPlaneY(origin, dir, 0.0f, ground)) {
        return false;
    }

    float cf = (ground[0] - map.x0) / map.tile + 0.5f;
    float rf = (ground[2] - map.z0) / map.tile + 0.5f;

    int32_t col = (int32_t)floorf(cf);
    int32_t row = (int32_t)floorf(rf);

    if (!oreOnGrid(row, col)) {
        return false;
    }

    *out = (OreCell){
        .row = row, .col = col,
        .fx = cf - (float)col, .fz = rf - (float)row
    };

    return true;
}

bool oreTextSet(char **dst, const char *src) {
    if (*dst && !ecs_os_strcmp(*dst, src)) {
        return false;
    }

    ecs_os_free(*dst);
    *dst = ecs_os_strdup(src);
    return true;
}

bool oreDepositMine(ecs_world_t *world, OreGame *game, ecs_entity_t deposit) {
    if (!deposit || !ecs_is_alive(world, deposit)) {
        return false;
    }

    OreDeposit *d = ecs_get_mut(world, deposit, OreDeposit);
    if (!d) {
        return false;
    }

    const OreResource *resource = ecs_get(world, d->resource, OreResource);
    if (!resource) {
        return false;
    }

    int32_t amount = resource->mine_amount;
    if (amount > d->amount) {
        amount = d->amount;
    }

    if (!oreInventoryAdd(world, d->resource, amount)) {
        return true;
    }

    const FlecsPosition3 *pos = ecs_get(world, deposit, FlecsPosition3);
    float x = pos ? pos->x : 0;
    float z = pos ? pos->z : 0;

    oreBurst(world, game->glow_pool, game->mine_burst, x, 0.6f, z);

    d->amount -= amount;

    if (d->amount <= 0) {
        oreBurst(world, game->fx_pool, game->poof_burst, x, 0.4f, z);
        ecs_delete(world, deposit);
        return false;
    }

    if (d->capacity > 0) {
        float scale_min = ecs_const_var_get_t(
            world, "cfg.depositScaleMin", ecs_f32_t);
        float frac = (float)d->amount / (float)d->capacity;
        float s = scale_min + (1.0f - scale_min) * frac;
        ecs_set(world, deposit, FlecsScale3, {s, s, s});
    }

    return true;
}

const char* oreDocText(const char *src, char *dst, int32_t size) {
    int32_t len = src ? (int32_t)ecs_os_strlen(src) : 0;

    if (len >= 2 && src[0] == '"' && src[len - 1] == '"') {
        src ++;
        len -= 2;
    }

    if (len > size - 1) {
        len = size - 1;
    }

    ecs_os_memcpy(dst, src, len);
    dst[len] = 0;
    return dst;
}

const char* oreItemName(
    ecs_world_t *world,
    ecs_entity_t item,
    char *dst,
    int32_t size)
{
    return oreDocText(item ? ecs_doc_get_name(world, item) : NULL, dst, size);
}

void oreSetState(ecs_world_t *world, OreGameState state) {
    ecs_singleton_add_pair(world, OreGameState,
        ecs_constant_to_entity(world, OreGameState, state));
}

static void OreDepositOnSet(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    OreDeposit *deposit = ecs_field(it, OreDeposit, 0);
    const FlecsPosition3 *pos = ecs_field(it, FlecsPosition3, 1);
    OreGame *game = ecs_field(it, OreGame, 2);

    OreMap map = ecs_const_var_get_t(world, "cfg.map", OreMap);

    ecs_defer_begin(world);

    for (int i = 0; i < it->count; i ++) {
        int32_t col = oreColAt(&map, pos[i].x);
        int32_t row = oreRowAt(&map, pos[i].z);
        if (!oreOnGrid(row, col)) {
            continue;
        }

        ecs_entity_t *cell = oreDepositCell(game, row, col);

        if (*cell && *cell != it->entities[i] && ecs_is_alive(world, *cell)) {
            ecs_add_id(world, it->entities[i], OreScrap);
            continue;
        }

        deposit[i].row = row;
        deposit[i].col = col;

        if (deposit[i].capacity < deposit[i].amount) {
            deposit[i].capacity = deposit[i].amount;
        }

        *cell = it->entities[i];
    }

    ecs_defer_end(world);
}

static void OreDepositOnRemove(ecs_iter_t *it) {
    const OreDeposit *deposit = ecs_field(it, OreDeposit, 0);
    OreGame *game = ecs_field(it, OreGame, 1);

    for (int i = 0; i < it->count; i ++) {
        if (!oreOnGrid(deposit[i].row, deposit[i].col)) {
            continue;
        }

        ecs_entity_t *cell = oreDepositCell(
            game, deposit[i].row, deposit[i].col);
        if (*cell == it->entities[i]) {
            *cell = 0;
        }
    }
}

static void OreScrapClean(ecs_iter_t *it) {
    for (int i = 0; i < it->count; i ++) {
        ecs_delete(it->world, it->entities[i]);
    }
}

static void OreClockUpdate(ecs_iter_t *it) {
    OreClock *clock = ecs_field(it, OreClock, 0);

    clock->time_elapsed += it->delta_time;

    ecs_singleton_modified(it->world, OreClock);
}

static void OreLuminiteProgressUpdate(ecs_iter_t *it) {
    OreLuminiteProgress *luminite = ecs_field(it, OreLuminiteProgress, 0);

    const OreRocketState *rocket = ecs_singleton_get(it->world, OreRocketState);

    int32_t held = (rocket && rocket->kit.luminite)
        ? oreInventoryGet(it->world, rocket->kit.luminite)
        : 0;

    if (luminite->luminite_held == held) {
        return;
    }

    luminite->luminite_held = held;

    ecs_singleton_modified(it->world, OreLuminiteProgress);
}

bool orePlaying(ecs_world_t *world) {
    ecs_entity_t playing = ecs_constant_to_entity(
        world, OreGameState, OreGameStatePlaying);
    return ecs_has_pair(world, ecs_id(OreGameState),
        ecs_id(OreGameState), playing);
}

bool oreTitle(ecs_world_t *world) {
    ecs_entity_t title = ecs_constant_to_entity(
        world, OreGameState, OreGameStateTitle);
    return ecs_has_pair(world, ecs_id(OreGameState),
        ecs_id(OreGameState), title);
}

static void OreTitleAnyKey(ecs_iter_t *it) {
    const FlecsInput *input = ecs_field(it, FlecsInput, 0);

    bool pressed = input->mouse.left.pressed || input->mouse.right.pressed;

    int32_t key_count = (int32_t)(sizeof(input->keys) / sizeof(input->keys[0]));
    for (int32_t k = 0; !pressed && k < key_count; k ++) {
        pressed = input->keys[k].pressed;
    }

    if (pressed) {
        oreSetState(it->world, OreGameStatePlaying);
    }
}

void oreResolve(ecs_script_future_t *future, bool value) {
    ecs_script_future_resolve(future,
        &(ecs_value_t){ecs_id(ecs_bool_t), &value});
    ecs_script_future_release(future);
}

static void oreRestart(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    (void)argc;
    (void)argv;
    ecs_world_t *world = ctx->world;
    if (orePlaying(world) || orePaused(world) || oreTitle(world)) {
        oreResolve(future, false);
        return;
    }

    OreGame *game = ecs_singleton_ensure(world, OreGame);

    ecs_delete_with(world, ecs_pair(EcsChildOf, game->buildings));
    ecs_delete_with(world, ecs_pair(EcsChildOf, game->critters));
    ecs_delete_with(world, ecs_pair(EcsChildOf, game->projectiles));

    oreRulesClear(world, game);

    memset(game->deposit_grid, 0, sizeof(game->deposit_grid));
    memset(game->building_grid, 0, sizeof(game->building_grid));

    OreCraftState *craft = ecs_singleton_ensure(world, OreCraftState);
    oreCraftClear(world, craft);
    ecs_singleton_modified(world, OreCraftState);

    game->selected = 0;

    OreUiState *ui = ecs_singleton_ensure(world, OreUiState);
    memset(ui->slots, 0, sizeof(ui->slots));
    ecs_singleton_modified(world, OreUiState);

    if (game->player && ecs_is_alive(world, game->player)) {
        ecs_set(world, game->player, OreMineProgress, {0});
    }

    OreRocketState *rocket = ecs_singleton_ensure(world, OreRocketState);
    oreRocketReset(world, rocket);
    ecs_singleton_modified(world, OreRocketState);

    if (!ecs_script(world, { .filename = "etc/player.flecs" })) {
        ecs_err("failed to reload etc/player.flecs");
    }

    oreSeedMap(world);

    if (!ecs_script(world, { .filename = "etc/scenery.flecs" })) {
        ecs_err("failed to reload etc/scenery.flecs");
    }

    if (!ecs_script(world, { .filename = "etc/deposits.flecs" })) {
        ecs_err("failed to reload etc/deposits.flecs");
    }

    if (!ecs_script(world, { .filename = "etc/scene.flecs" })) {
        ecs_err("failed to reload etc/scene.flecs");
    }

    oreStressTestApply(world);
    oreResolve(future, true);
}

static const char *ore_seed_names[] = {
    "mapSeed", "mapSeedCenter", "mapSeedScenery", "mapSeedFlora", "mapSeedOre",
    "mapSeedProp"
};

void oreSeedMap(ecs_world_t *world) {
    static uint64_t counter = 0;

    uint64_t base;
    const char *fixed = getenv("ORE_SEED");
    if (fixed) {
        base = strtoull(fixed, NULL, 10);
    } else {
        base = (uint64_t)time(NULL) * 0x9E3779B97F4A7C15ull;
        base ^= (uint64_t)(uintptr_t)world;
    }

    base += (counter ++) * 0xBF58476D1CE4E5B9ull;

    int32_t count = (int32_t)(sizeof(ore_seed_names) /
        sizeof(ore_seed_names[0]));

    for (int32_t i = 0; i < count; i ++) {
        uint64_t v = base + (uint64_t)i * 0x94D049BB133111EBull;
        v ^= v >> 31;
        v *= 0xD6E8FEB86659FD93ull;
        v ^= v >> 32;
        if (!v) {
            v = 1;
        }

        ecs_entity_t prev = ecs_lookup(world, ore_seed_names[i]);
        if (prev) {
            ecs_delete(world, prev);
        }

        ecs_const_var(world, {
            .name = ore_seed_names[i],
            .type = ecs_id(ecs_u64_t),
            .value = &v
        });
    }
}

ecs_entity_t oreChild(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    if (!parent) {
        return 0;
    }

    ecs_entity_t result = ecs_lookup_child(world, parent, name);
    if (result) {
        return result;
    }

    ecs_iter_t it = ecs_children(world, parent);
    while (ecs_children_next(&it)) {
        for (int i = 0; i < it.count; i ++) {
            ecs_entity_t base = ecs_get_target(
                world, it.entities[i], EcsIsA, 0);
            if (!base) {
                continue;
            }
            const char *base_name = ecs_get_name(world, base);
            if (base_name && !ecs_os_strcmp(base_name, name)) {
                result = it.entities[i];
                ecs_iter_fini(&it);
                return result;
            }
        }
    }

    return 0;
}

void oreGameImport(ecs_world_t *world) {
    ECS_META_COMPONENT(world, OreGameState);
    ECS_META_COMPONENT(world, OreMap);
    ECS_META_COMPONENT(world, OreClock);
    ECS_META_COMPONENT(world, OreLuminiteProgress);
    ECS_META_COMPONENT(world, OreGame);
    ECS_TAG_DEFINE(world, OreScrap);

    ecs_add_id(world, ecs_id(OreGameState), EcsExclusive);
    ecs_add_id(world, ecs_id(OreGameState), EcsSingleton);
    ecs_add_id(world, ecs_id(OreClock), EcsSingleton);
    ecs_add_id(world, ecs_id(OreLuminiteProgress), EcsSingleton);
    ecs_add_id(world, ecs_id(OreGame), EcsSingleton);

    ecs_singleton_ensure(world, OreGame);
    ecs_singleton_modified(world, OreGame);

    ecs_singleton_ensure(world, OreClock);
    ecs_singleton_modified(world, OreClock);

    ecs_singleton_ensure(world, OreLuminiteProgress);
    ecs_singleton_modified(world, OreLuminiteProgress);

    ecs_async_function(world, {
        .name = "restart",
        .parent = ecs_get_scope(world),
        .return_type = ecs_id(ecs_bool_t),
        .callback = oreRestart
    });

    ecs_add_pair(world, ecs_id(OreDeposit), EcsWith, FlecsDynamicTransform);

    ecs_entity_t playing = ecs_constant_to_entity(
        world, OreGameState, OreGameStatePlaying);
    ecs_entity_t title = ecs_constant_to_entity(
        world, OreGameState, OreGameStateTitle);

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "OreDepositOnSet" }),
        .query.terms = {
            { .id = ecs_id(OreDeposit) },
            { .id = ecs_id(FlecsPosition3) },
            { .id = ecs_id(OreGame) }
        },
        .events = { EcsOnSet },
        .callback = OreDepositOnSet
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "OreDepositOnRemove" }),
        .query.terms = {
            { .id = ecs_id(OreDeposit) },
            { .id = ecs_id(OreGame) }
        },
        .events = { EcsOnRemove },
        .callback = OreDepositOnRemove
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreScrapClean" }),
        .phase = EcsPreUpdate,
        .query.terms = {
            { .id = OreScrap }
        },
        .callback = OreScrapClean
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreClockUpdate" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreClock) },
            { .id = ecs_pair_t(OreGameState, playing) }
        },
        .callback = OreClockUpdate
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreTitleAnyKey" }),
        .phase = EcsPreUpdate,
        .query.terms = {
            { .id = ecs_id(FlecsInput), .src.id = ecs_id(FlecsInput),
                .inout = EcsIn },
            { .id = ecs_pair_t(OreGameState, title) }
        },
        .callback = OreTitleAnyKey
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreLuminiteProgressUpdate" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreLuminiteProgress) },
            { .id = ecs_pair_t(OreGameState, playing) }
        },
        .callback = OreLuminiteProgressUpdate
    });
}

static void OreUiClick(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    for (int i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];

        if (ecs_has(world, e, OreSlot)) {
            oreSlotClick(world, e);
        }

        if (ecs_has(world, e, OreTab)) {
            oreTabClick(world, e);
        }

        if (ecs_has(world, e, OreRocketPacket)) {
            oreRocketClick(world, e);
        }

        if (ecs_has(world, e, OrePowerSeg)) {
            orePowerSegClick(world, e);
        }

        if (ecs_has(world, e, OreQualityCycle)) {
            oreQualityClick(world, e);
        }
    }
}

void Ore_elseImport(ecs_world_t *world) {
    ECS_MODULE(world, Ore_else);

    ecs_set_name_prefix(world, "Ore");

    oreResourcesImport(world);
    oreGameImport(world);
    orePlayerImport(world);
    orePowerImport(world);
    oreBuildingsImport(world);
    oreCraftImport(world);
    oreRulesImport(world);
    oreUiImport(world);
    orePlacementImport(world);
    oreCombatImport(world);
    oreRocketImport(world);
    oreHealthBarsImport(world);
    oreDronesImport(world);
    oreQualityImport(world);

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "OreUiClick" }),
        .query.terms = {
            { .id = ecs_id(OreSlot), .oper = EcsOr,
              .inout = EcsInOutNone },
            { .id = ecs_id(OreTab), .oper = EcsOr,
              .inout = EcsInOutNone },
            { .id = ecs_id(OreRocketPacket), .oper = EcsOr,
              .inout = EcsInOutNone },
            { .id = ecs_id(OrePowerSeg), .oper = EcsOr,
              .inout = EcsInOutNone },
            { .id = OreQualityCycle, .inout = EcsInOutNone }
        },
        .events = { FlecsUiClicked },
        .callback = OreUiClick
    });
}
