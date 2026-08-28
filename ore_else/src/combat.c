#define ORE_ELSE_COMBAT_IMPL
#include "ore_else.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define ORE_CROWD_ORIGIN (-72.0f)
#define ORE_CROWD_SPAN (144.0f)
#define ORE_CROWD_NEAR (12)

typedef struct {
    float *x;
    float *z;
    float *r;
    float *px;
    float *pz;
    int32_t *cell;
    int32_t *order;
    int32_t *start;
    int32_t cap;
    int32_t cells;
    int32_t dim;
} OreCrowd;

typedef struct {
    float *x;
    float *z;
    ecs_entity_t *id;
    const OreHealth **hp;
    int32_t *cell;
    int32_t *order;
    int32_t *start;
    int32_t n;
    int32_t cap;
    int32_t cells;
    int32_t dim;
    float size;
    bool valid;
} OreCritterIndex;

static bool oreCritterIndexReserve(
    OreCritterIndex *index,
    int32_t count,
    int32_t cells)
{
    if (count > index->cap) {
        int32_t cap = index->cap ? index->cap : 256;
        while (cap < count) {
            cap *= 2;
        }

        ecs_os_free(index->x);
        ecs_os_free(index->z);
        ecs_os_free(index->id);
        ecs_os_free(index->hp);
        ecs_os_free(index->cell);
        ecs_os_free(index->order);

        index->x = ecs_os_malloc_n(float, cap);
        index->z = ecs_os_malloc_n(float, cap);
        index->id = ecs_os_malloc_n(ecs_entity_t, cap);
        index->hp = ecs_os_malloc_n(const OreHealth*, cap);
        index->cell = ecs_os_malloc_n(int32_t, cap);
        index->order = ecs_os_malloc_n(int32_t, cap);
        index->cap = cap;
    }

    if (cells + 1 > index->cells) {
        ecs_os_free(index->start);
        index->start = ecs_os_malloc_n(int32_t, cells + 1);
        index->cells = cells + 1;
    }

    return index->x != NULL && index->start != NULL;
}

static void oreCritterIndexBuild(
    ecs_world_t *world,
    OreCombatState *combat)
{
    OreCritterIndex *index = combat->index;
    if (!index) {
        index = ecs_os_calloc_t(OreCritterIndex);
        combat->index = index;
    }

    index->valid = false;

    float size = ecs_const_var_get_t(world, "cfg.crowdCell", ecs_f32_t);
    if (size <= 0) {
        return;
    }

    int32_t dim = (int32_t)(ORE_CROWD_SPAN / size) + 1;
    int32_t cells = dim * dim;

    int32_t n = 0;
    ecs_iter_t it = ecs_query_iter(world, combat->critter_query);
    while (ecs_query_next(&it)) {
        n += it.count;
    }

    if (!oreCritterIndexReserve(index, n, cells)) {
        return;
    }

    int32_t k = 0;
    it = ecs_query_iter(world, combat->critter_query);
    while (ecs_query_next(&it)) {
        const OreHealth *health = ecs_field(&it, OreHealth, 1);
        const FlecsPosition3 *pos = ecs_field(&it, FlecsPosition3, 2);

        for (int i = 0; i < it.count && k < n; i ++) {
            if (health[i].value <= 0) {
                continue;
            }

            index->x[k] = pos[i].x;
            index->z[k] = pos[i].z;
            index->id[k] = it.entities[i];
            index->hp[k] = &health[i];
            k ++;
        }
    }

    n = k;

    ecs_os_memset(index->start, 0,
        (ecs_size_t)(cells + 1) * ECS_SIZEOF(int32_t));

    for (int32_t i = 0; i < n; i ++) {
        int32_t cx = (int32_t)((index->x[i] - ORE_CROWD_ORIGIN) / size);
        int32_t cz = (int32_t)((index->z[i] - ORE_CROWD_ORIGIN) / size);

        if (cx < 0) cx = 0;
        if (cx >= dim) cx = dim - 1;
        if (cz < 0) cz = 0;
        if (cz >= dim) cz = dim - 1;

        index->cell[i] = cz * dim + cx;
        index->start[index->cell[i] + 1] ++;
    }

    for (int32_t c = 0; c < cells; c ++) {
        index->start[c + 1] += index->start[c];
    }

    for (int32_t i = 0; i < n; i ++) {
        index->order[index->start[index->cell[i]] ++] = i;
    }

    for (int32_t c = cells; c > 0; c --) {
        index->start[c] = index->start[c - 1];
    }

    index->start[0] = 0;
    index->n = n;
    index->dim = dim;
    index->size = size;
    index->valid = true;
}

static ecs_entity_t oreCritterIndexNearest(
    const OreCritterIndex *index,
    float x,
    float z,
    float *best_d2)
{
    float size = index->size;
    int32_t dim = index->dim;
    float range = sqrtf(*best_d2);
    int32_t reach = (int32_t)ceilf(range / size);

    int32_t cx = (int32_t)((x - ORE_CROWD_ORIGIN) / size);
    int32_t cz = (int32_t)((z - ORE_CROWD_ORIGIN) / size);

    int32_t x0 = cx - reach, x1 = cx + reach;
    int32_t z0 = cz - reach, z1 = cz + reach;
    if (x0 < 0) x0 = 0;
    if (z0 < 0) z0 = 0;
    if (x1 >= dim) x1 = dim - 1;
    if (z1 >= dim) z1 = dim - 1;

    ecs_entity_t best = 0;

    for (int32_t nz = z0; nz <= z1; nz ++) {
        for (int32_t nx = x0; nx <= x1; nx ++) {
            int32_t c = nz * dim + nx;
            int32_t b = index->start[c];
            int32_t e = index->start[c + 1];

            for (; b < e; b ++) {
                int32_t j = index->order[b];
                if (index->hp[j]->value <= 0) {
                    continue;
                }

                float dx = index->x[j] - x;
                float dz = index->z[j] - z;
                float d2 = dx * dx + dz * dz;

                if (d2 < *best_d2) {
                    *best_d2 = d2;
                    best = index->id[j];
                }
            }
        }
    }

    return best;
}

static void OreIndexCritters(ecs_iter_t *it) {
    OreCombatState *combat = ecs_field(it, OreCombatState, 0);
    oreCritterIndexBuild(it->world, combat);
}

static void oreDamageBuilding(
    ecs_world_t *world,
    OreGame *game,
    const OreArsenal *arsenal,
    ecs_entity_t building,
    float dmg)
{
    if (!building || !ecs_is_alive(world, building)) {
        return;
    }

    OreHealth *health = ecs_get_mut(world, building, OreHealth);
    if (!health || health->value <= 0) {
        return;
    }

    const FlecsPosition3 *pos = ecs_get(world, building, FlecsPosition3);
    if (!pos) {
        return;
    }

    health->value -= dmg;

    if (health->value <= 0) {
        oreExplosion(world, game, arsenal->boom_burst,
            pos->x, 0.55f, pos->z, 0.8f);
        ecs_delete(world, building);
        return;
    }

    ecs_modified(world, building, OreHealth);
}

static void oreCharParts(
    ecs_world_t *world,
    ecs_entity_t parent,
    const FlecsRgba *color)
{
    ecs_iter_t it = ecs_children(world, parent);
    while (ecs_children_next(&it)) {
        for (int i = 0; i < it.count; i ++) {
            ecs_entity_t child = it.entities[i];

            if (ecs_has(world, child, FlecsRgba)) {
                ecs_set_ptr(world, child, FlecsRgba, color);
            }

            if (ecs_has(world, child, FlecsEmissive)) {
                ecs_set(world, child, FlecsEmissive, {0, *color});
            }

            oreCharParts(world, child, color);
        }
    }
}

static void oreCorpseCull(
    ecs_world_t *world,
    const OreCombatState *combat,
    int32_t cap)
{
    if (cap <= 0) {
        return;
    }

    int32_t count = 0;
    float worst = 0;
    ecs_entity_t oldest = 0;

    ecs_iter_t it = ecs_query_iter(world, combat->corpse_query);
    while (ecs_query_next(&it)) {
        const OreCorpse *corpse = ecs_field(&it, OreCorpse, 0);

        for (int i = 0; i < it.count; i ++) {
            count ++;

            if (!oldest || corpse[i].timer < worst) {
                worst = corpse[i].timer;
                oldest = it.entities[i];
            }
        }
    }

    if (count >= cap && oldest) {
        ecs_delete(world, oldest);
    }
}

static void oreCorpseMake(
    ecs_world_t *world,
    const OreArsenal *arsenal,
    const OreCombatState *combat,
    ecs_entity_t critter,
    const FlecsPosition3 *pos)
{
    float life = ecs_const_var_get_t(world, "cfg.corpseTime", ecs_f32_t);
    float sink = ecs_const_var_get_t(world, "cfg.corpseSink", ecs_f32_t);
    int32_t cap = ecs_const_var_get_t(world, "cfg.corpseCap", ecs_i32_t);
    FlecsRgba char_color = ecs_const_var_get_t(
        world, "cfg.corpseColor", FlecsRgba);

    if (life <= 0) {
        ecs_delete(world, critter);
        return;
    }

    oreCorpseCull(world, combat, cap);

    const OreRadius *radius = ecs_get(world, critter, OreRadius);
    float r = radius ? radius->value : 0.5f;

    const FlecsRotation3 *rot = ecs_get(world, critter, FlecsRotation3);
    float yaw = rot ? rot->y : 0;

    ecs_remove(world, critter, OreCritter);
    ecs_remove(world, critter, OreTarget);

    ecs_set(world, critter, OreCorpse, {life, sink, r * 2.0f + 1.0f});
    ecs_set(world, critter, FlecsRotation3, {
        1.1f + oreRandf() * 0.2f, yaw, (oreRandf() - 0.5f) * 0.8f});
    ecs_set(world, critter, FlecsScale3, {1.0f, 0.55f, 1.0f});
    ecs_set(world, critter, FlecsPosition3, {pos->x, 0, pos->z});

    oreCharParts(world, critter, &char_color);

    if (arsenal->embers) {
        float s = r * 2.0f;
        ecs_entity_t embers = ecs_new_w_pair(
            world, EcsIsA, arsenal->embers);
        ecs_set(world, embers, EcsParent, { critter });
        ecs_set(world, embers, FlecsPosition3, {0, r * 0.5f, 0});
        ecs_set(world, embers, FlecsRotation3, {0, oreRandf() * 6.2832f, 0});
        ecs_set(world, embers, FlecsScale3, {s, s, s});
    }
}

static void oreDamageCritter(
    ecs_world_t *world,
    OreGame *game,
    const OreArsenal *arsenal,
    const OreCombatState *combat,
    OreWaveState *waves,
    ecs_entity_t critter,
    float dmg)
{
    if (!critter || !ecs_is_alive(world, critter)) {
        return;
    }

    OreHealth *health = ecs_get_mut(world, critter, OreHealth);
    if (!health || health->value <= 0) {
        return;
    }

    health->value -= dmg;

    if (health->value > 0) {
        ecs_modified(world, critter, OreHealth);
        return;
    }

    health->value = 0;

    const FlecsPosition3 *pos = ecs_get(world, critter, FlecsPosition3);
    if (pos) {
        const OreDeathBurst *death = ecs_get(world, critter, OreDeathBurst);
        if (death && death->burst) {
            const OreRadius *radius = ecs_get(world, critter, OreRadius);
            oreCombust(world, game, death->burst,
                pos->x, pos->y, pos->z, radius ? radius->value : 0.5f);
        } else {
            oreBurst(world, game->fx_pool, arsenal->splat_burst,
                pos->x, pos->y + 0.6f, pos->z);
        }
    }

    waves->kills ++;

    if (pos && ecs_has(world, critter, OreCritter)) {
        oreCorpseMake(world, arsenal, combat, critter, pos);
    } else {
        ecs_delete(world, critter);
    }
}

static void OreCorpses(ecs_iter_t *it) {
    OreCorpse *corpse = ecs_field(it, OreCorpse, 0);
    FlecsPosition3 *pos = ecs_field(it, FlecsPosition3, 1);

    for (int i = 0; i < it->count; i ++) {
        corpse[i].timer -= it->delta_time;

        if (corpse[i].timer > 0) {
            continue;
        }

        pos[i].y -= corpse[i].sink * it->delta_time;

        if (pos[i].y < -corpse[i].depth) {
            ecs_delete(it->world, it->entities[i]);
        }
    }
}

static void oreDamagePlayer(
    ecs_world_t *world,
    OreGame *game,
    const OreArsenal *arsenal,
    const OreRocketState *rocket,
    float dmg)
{
    if (rocket && rocket->boarded) {
        return;
    }

    OrePlayer *player = ecs_get_mut(world, game->player, OrePlayer);
    OreHealth *health = ecs_get_mut(world, game->player, OreHealth);
    if (!player || !health || health->value <= 0) {
        return;
    }

    health->value -= dmg;
    player->hurt = 0;

    if (health->value > 0) {
        ecs_modified(world, game->player, OreHealth);
        return;
    }

    health->value = 0;
    ecs_modified(world, game->player, OreHealth);

    const FlecsPosition3 *pos = ecs_get(world, game->player, FlecsPosition3);
    if (pos) {
        oreBurst(world, game->fx_pool, arsenal->splat_burst,
            pos->x, pos->y + 1.1f, pos->z);
    }

    ecs_set(world, game->player, FlecsRotation3, {1.5708f, 0, 0});

    oreSetState(world, OreGameStateLost);
}

static void oreSpawnProjectile(
    ecs_world_t *world,
    OreGame *game,
    ecs_entity_t prefab,
    float x,
    float y,
    float z,
    float vx,
    float vy,
    float vz,
    float dmg,
    const OreSplash *splash,
    bool hits_critters,
    ecs_entity_t target)
{
    if (!prefab) {
        return;
    }

    const OreProjectile *base = ecs_get(world, prefab, OreProjectile);
    if (!base) {
        return;
    }

    OreProjectile shot = *base;
    shot.vx = vx;
    shot.vy = vy;
    shot.vz = vz;
    shot.hits_critters = hits_critters;

    ecs_entity_t e = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_add_pair(world, e, EcsChildOf, game->projectiles);
    ecs_set(world, e, FlecsPosition3, {x, y, z});
    ecs_set(world, e, FlecsRotation3, {0, atan2f(vx, vz), 0});
    ecs_set(world, e, FlecsScale3, {1, 1, 1});
    ecs_set(world, e, OreDamage, {dmg});

    if (splash) {
        ecs_set(world, e, OreSplash, {splash->radius});
    }

    if (target && ecs_has(world, prefab, OreHoming)) {
        ecs_set(world, e, OreTarget, {target});
    }

    ecs_set_id(world, e, ecs_id(OreProjectile), sizeof(OreProjectile), &shot);
}

static ecs_entity_t oreNearest(
    ecs_world_t *world,
    ecs_query_t *query,
    float x,
    float z,
    float *best_d2)
{
    ecs_entity_t best = 0;

    ecs_iter_t it = ecs_query_iter(world, query);
    while (ecs_query_next(&it)) {
        const OreHealth *health = ecs_field(&it, OreHealth, 1);
        const FlecsPosition3 *pos = ecs_field(&it, FlecsPosition3, 2);

        for (int i = 0; i < it.count; i ++) {
            if (health[i].value <= 0) {
                continue;
            }

            float dx = pos[i].x - x;
            float dz = pos[i].z - z;
            float d2 = dx * dx + dz * dz;

            if (d2 < *best_d2) {
                *best_d2 = d2;
                best = it.entities[i];
            }
        }
    }

    return best;
}

static ecs_entity_t oreNearestCritter(
    ecs_world_t *world,
    const OreCombatState *combat,
    float x,
    float z,
    float range)
{
    float d2 = range * range;
    const OreCritterIndex *index = combat->index;
    if (index && index->valid) {
        return oreCritterIndexNearest(index, x, z, &d2);
    }

    return oreNearest(world, combat->critter_query, x, z, &d2);
}

static bool orePlayerWithin(
    ecs_world_t *world,
    const OreGame *game,
    const OreRocketState *rocket,
    float x,
    float z,
    float *best_d2)
{
    const OreHealth *player = ecs_get(world, game->player, OreHealth);
    const FlecsPosition3 *ppos = ecs_get(world, game->player, FlecsPosition3);

    if (!player || !ppos || player->value <= 0 || (rocket && rocket->boarded)) {
        return false;
    }

    float dx = ppos->x - x;
    float dz = ppos->z - z;
    float d2 = dx * dx + dz * dz;

    if (d2 >= *best_d2) {
        return false;
    }

    *best_d2 = d2;
    return true;
}

static ecs_entity_t oreCritterAcquire(
    ecs_world_t *world,
    const OreGame *game,
    const OreCombatState *combat,
    const OreRocketState *rocket,
    float x,
    float z,
    float range)
{
    float best_d2 = range * range;
    ecs_entity_t best = orePlayerWithin(world, game, rocket, x, z, &best_d2)
        ? game->player : 0;
    ecs_entity_t other = oreNearest(world, combat->target_query, x, z, &best_d2);

    return other ? other : best;
}

static void oreSpawnCritter(
    ecs_world_t *world,
    OreGame *game,
    ecs_entity_t prefab,
    float x,
    float z)
{
    const OreHealth *base_health = ecs_get(world, prefab, OreHealth);
    const OreCritter *base_critter = ecs_get(world, prefab, OreCritter);
    if (!base_health || !base_critter) {
        return;
    }

    const OreAttackInterval *interval = ecs_get(
        world, prefab, OreAttackInterval);

    float retarget = ecs_const_var_get_t(
        world, "cfg.retargetInterval", ecs_f32_t);

    OreCritter critter = *base_critter;
    critter.retarget = retarget * oreRandf();
    critter.anim = oreRandf() * 6.2832f;
    critter.jitter = (oreRandf() - 0.5f) * 2.0f;
    critter.attacking = false;

    ecs_entity_t e = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_add_pair(world, e, EcsChildOf, game->critters);
    ecs_set(world, e, FlecsPosition3, {x, 0, z});
    ecs_set(world, e, FlecsRotation3, {0, 0, 0});
    ecs_set(world, e, FlecsScale3, {1, 1, 1});
    ecs_set(world, e, OreHealth, {base_health->value, base_health->value});
    ecs_set(world, e, OreTarget, {0});

    if (interval) {
        ecs_set(world, e, OreAttackTimer, {interval->value * oreRandf()});
    }

    ecs_set_id(world, e, ecs_id(OreCritter), sizeof(OreCritter), &critter);
}

#define ORE_TIER_BEHEMOTH (ORE_TIERS - 1)

static bool oreWaveTierFull(
    int32_t tier,
    const int32_t *out,
    int32_t behemoth_cap)
{
    return tier == ORE_TIER_BEHEMOTH && behemoth_cap >= 0 &&
        out[tier] >= behemoth_cap;
}

static float oreWaveTopCost(
    int32_t wave,
    const int32_t *gate,
    const float *cost,
    const int32_t *out,
    int32_t behemoth_cap)
{
    float top = 0;

    for (int32_t t = 0; t < ORE_TIERS; t ++) {
        if (wave < gate[t] || cost[t] <= 0) {
            continue;
        }

        if (oreWaveTierFull(t, out, behemoth_cap)) {
            continue;
        }

        if (cost[t] > top) {
            top = cost[t];
        }
    }

    return top;
}

static float oreWaveBudget(ecs_world_t *world, int32_t wave) {
    if (wave < 1) {
        return 0;
    }

    float base = ecs_const_var_get_t(world, "cfg.waveBudgetBase", ecs_f32_t);
    float growth = ecs_const_var_get_t(
        world, "cfg.waveBudgetGrowth", ecs_f32_t);

    if (growth < 1.0f) {
        growth = 1.0f;
    }

    return base * powf(growth, (float)(wave - 1));
}

static void oreWaveCompose(
    ecs_world_t *world,
    OreWaveState *waves,
    int32_t wave,
    int32_t *out)
{
    OreWaveMix cost_mix = ecs_const_var_get_t(world, "cfg.tierCost", OreWaveMix);
    OreWaveGate gates = ecs_const_var_get_t(world, "cfg.tierGate", OreWaveGate);
    int32_t unit_cap = ecs_const_var_get_t(world, "cfg.waveUnitCap", ecs_i32_t);
    int32_t behemoth_cap = ecs_const_var_get_t(
        world, "cfg.waveBehemothCap", ecs_i32_t);
    float swarm_chance = ecs_const_var_get_t(world, "cfg.swarmChance", ecs_f32_t);
    float elite_chance = ecs_const_var_get_t(world, "cfg.eliteChance", ecs_f32_t);

    float roll = oreRandf();
    OreWaveStyle style = OreWaveStyleMixed;
    const char *mix_var = "cfg.mixedMix";

    if (roll < swarm_chance) {
        style = OreWaveStyleSwarm;
        mix_var = "cfg.swarmMix";
    } else if (roll < swarm_chance + elite_chance) {
        style = OreWaveStyleElite;
        mix_var = "cfg.eliteMix";
    }

    OreWaveMix style_mix = ecs_const_var_get_t(world, mix_var, OreWaveMix);

    const float *cost = cost_mix.tier;
    const float *share = style_mix.tier;
    const int32_t *gate = gates.tier;
    float weight[ORE_TIERS];

    for (int32_t t = 0; t < ORE_TIERS; t ++) {
        out[t] = 0;
        weight[t] = cost[t] > 0 ? share[t] / cost[t] : 0;
    }

    float top = oreWaveTopCost(wave, gate, cost, out, behemoth_cap);

    float budget = oreWaveBudget(world, wave);

    waves->budget = budget;
    waves->style = style;

    if (top <= 0 || unit_cap <= 0) {
        return;
    }

    float left = budget;
    int32_t units = 0;

    while (units < unit_cap) {
        float slots = (float)(unit_cap - units - 1);
        float total = 0;
        float w[ORE_TIERS];
        int32_t dear = -1;
        int32_t fit = -1;

        for (int32_t t = 0; t < ORE_TIERS; t ++) {
            w[t] = 0;

            if (wave < gate[t] || cost[t] <= 0 || cost[t] > left) {
                continue;
            }

            if (oreWaveTierFull(t, out, behemoth_cap)) {
                continue;
            }

            if (dear < 0 || cost[t] > cost[dear]) {
                dear = t;
            }

            if (left - cost[t] > top * slots) {
                continue;
            }

            fit = t;
            w[t] = weight[t];
            total += w[t];
        }

        int32_t pick = -1;

        if (total > 0) {
            float r = oreRandf() * total;

            for (int32_t t = 0; t < ORE_TIERS; t ++) {
                if (w[t] <= 0) {
                    continue;
                }

                r -= w[t];

                if (r <= 0) {
                    pick = t;
                    break;
                }
            }

            if (pick < 0) {
                pick = fit;
            }
        } else if (dear >= 0) {
            pick = dear;
        }

        if (pick < 0) {
            break;
        }

        out[pick] ++;
        units ++;
        left -= cost[pick];

        if (oreWaveTierFull(pick, out, behemoth_cap)) {
            top = oreWaveTopCost(wave, gate, cost, out, behemoth_cap);

            if (top <= 0) {
                break;
            }
        }
    }
}

static ecs_entity_t oreWaveNextPrefab(
    const OreArsenal *arsenal,
    OreWaveState *waves)
{
    int32_t *left[ORE_TIERS] = {
        &waves->spawn_small,
        &waves->spawn_medium,
        &waves->spawn_big,
        &waves->spawn_huge
    };

    ecs_entity_t prefab[ORE_TIERS] = {
        arsenal->mite,
        arsenal->skitter,
        arsenal->brute,
        arsenal->behemoth
    };

    int32_t total = 0;

    for (int32_t t = 0; t < ORE_TIERS; t ++) {
        if (*left[t] > 0 && prefab[t]) {
            total += *left[t];
        }
    }

    if (!total) {
        return 0;
    }

    int32_t r = (int32_t)(oreRandf() * (float)total);
    if (r >= total) {
        r = total - 1;
    }

    for (int32_t t = 0; t < ORE_TIERS; t ++) {
        if (*left[t] <= 0 || !prefab[t]) {
            continue;
        }

        r -= *left[t];

        if (r < 0) {
            (*left[t]) --;
            return prefab[t];
        }
    }

    return 0;
}

static int32_t ore_prev_side = -1;

static int32_t oreNextSpawnSide(void) {
    int32_t side = (int32_t)(oreRandf() * 3.0f);
    if (side > 2) {
        side = 2;
    }

    if (ore_prev_side >= 0 && side >= ore_prev_side) {
        side ++;
    }

    ore_prev_side = side;

    return side;
}

static void oreSpawnWaveCritter(
    ecs_world_t *world,
    OreGame *game,
    const OreArsenal *arsenal,
    OreWaveState *waves)
{
    ecs_entity_t prefab = oreWaveNextPrefab(arsenal, waves);
    if (!prefab) {
        return;
    }

    float jitter = ecs_const_var_get_t(
        world, "cfg.edgeSpawnJitter", ecs_f32_t);
    float inset = ecs_const_var_get_t(world, "cfg.edgeInset", ecs_f32_t);

    OreMap map = ecs_const_var_get_t(world, "cfg.map", OreMap);
    float min_x = oreTileX(&map, 0);
    float max_x = oreTileX(&map, ORE_COLS - 1);
    float min_z = oreTileZ(&map, 0);
    float max_z = oreTileZ(&map, ORE_ROWS - 1);

    float rim_min_x = min_x + inset;
    float rim_max_x = max_x - inset;
    float rim_min_z = min_z + inset;
    float rim_max_z = max_z - inset;

    int32_t side = oreNextSpawnSide();
    float t = oreRandf();

    float x, z;

    if (side == 0) {
        x = rim_min_x + t * (rim_max_x - rim_min_x);
        z = rim_min_z;
    } else if (side == 1) {
        x = rim_min_x + t * (rim_max_x - rim_min_x);
        z = rim_max_z;
    } else if (side == 2) {
        x = rim_min_x;
        z = rim_min_z + t * (rim_max_z - rim_min_z);
    } else {
        x = rim_max_x;
        z = rim_min_z + t * (rim_max_z - rim_min_z);
    }

    x += (oreRandf() - 0.5f) * 2.0f * jitter;
    z += (oreRandf() - 0.5f) * 2.0f * jitter;

    if (x < min_x) x = min_x;
    if (x > max_x) x = max_x;
    if (z < min_z) z = min_z;
    if (z > max_z) z = max_z;

    oreSpawnCritter(world, game, prefab, x, z);
}

static void OreWaves(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    OreGame *game = ecs_field(it, OreGame, 0);
    const OreArsenal *arsenal = ecs_field(it, OreArsenal, 1);
    OreWaveState *waves = ecs_field(it, OreWaveState, 2);
    const OreCombatState *combat = ecs_field(it, OreCombatState, 3);

    if (combat->index) {
        ((OreCritterIndex*)combat->index)->valid = false;
    }

    int32_t alive = 0;
    ecs_iter_t ait = ecs_query_iter(world, combat->critter_query);
    while (ecs_query_next(&ait)) {
        const OreHealth *health = ecs_field(&ait, OreHealth, 1);
        for (int i = 0; i < ait.count; i ++) {
            if (health[i].value > 0) {
                alive ++;
            }
        }
    }

    waves->alive = alive;

    if (waves->spawn_left > 0) {
        waves->spawn_timer -= it->delta_time;

        while (waves->spawn_timer <= 0 && waves->spawn_left > 0) {
            waves->spawn_timer += waves->stagger;
            waves->spawn_left --;
            oreSpawnWaveCritter(world, game, arsenal, waves);
        }

        if (waves->spawn_timer < 0) {
            waves->spawn_timer = 0;
        }
    } else {
        waves->timer -= it->delta_time;

        if (waves->timer <= 0) {
            float gap = ecs_const_var_get_t(
                world, "cfg.waveGap", ecs_f32_t);
            float stagger = ecs_const_var_get_t(
                world, "cfg.spawnStagger", ecs_f32_t);
            float window = ecs_const_var_get_t(
                world, "cfg.spawnWindow", ecs_f32_t);

            waves->wave ++;

            int32_t comp[ORE_TIERS];
            oreWaveCompose(world, waves, waves->wave, comp);

            waves->spawn_small = comp[0];
            waves->spawn_medium = comp[1];
            waves->spawn_big = comp[2];
            waves->spawn_huge = comp[3];
            waves->spawn_left =
                comp[0] + comp[1] + comp[2] + comp[3];
            waves->spawn_timer = 0;

            if (waves->spawn_left > 0 && window > 0) {
                float spread = window / (float)waves->spawn_left;
                if (spread < stagger) {
                    stagger = spread;
                }
            }

            waves->stagger = stagger;

            waves->timer = gap;
            waves->wave_span = gap;
        }
    }

    if (waves->wave_span < waves->timer) {
        waves->wave_span = waves->timer;
    }

    if (waves->spawn_left > 0) {
        waves->frac = 1.0f;
    } else if (waves->wave_span > 0) {
        waves->frac = 1.0f - waves->timer / waves->wave_span;
    } else {
        waves->frac = 0;
    }

    ecs_singleton_modified(world, OreWaveState);
}

static void OreCritters(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    OreCritter *critter = ecs_field(it, OreCritter, 0);
    const OreHealth *health = ecs_field(it, OreHealth, 1);
    const OreSpeed *speed = ecs_field(it, OreSpeed, 2);
    OreVelocity *vel = ecs_field(it, OreVelocity, 3);
    const OreDamage *dmg = ecs_field(it, OreDamage, 4);
    const OreRange *range = ecs_field(it, OreRange, 5);
    OreTarget *target_field = ecs_field(it, OreTarget, 6);
    const OreAttackInterval *interval = ecs_field(it, OreAttackInterval, 7);
    OreAttackTimer *timer = ecs_field(it, OreAttackTimer, 8);
    const OreMuzzle *muzzle = ecs_field(it, OreMuzzle, 9);
    const OreAmmo *ammo = ecs_field(it, OreAmmo, 10);
    FlecsPosition3 *pos = ecs_field(it, FlecsPosition3, 11);
    FlecsRotation3 *rot = ecs_field(it, FlecsRotation3, 12);
    OreGame *game = ecs_field(it, OreGame, 13);
    const OreArsenal *arsenal = ecs_field(it, OreArsenal, 14);
    const OreCombatState *combat = ecs_field(it, OreCombatState, 15);

    const OreRocketState *rocket = ecs_singleton_get(world, OreRocketState);

    bool ranged = interval && timer && muzzle && ammo;

    OreMap map = ecs_const_var_get_t(world, "cfg.map", OreMap);
    float aggro = ecs_const_var_get_t(world, "cfg.aggroRange", ecs_f32_t);
    float retarget = ecs_const_var_get_t(
        world, "cfg.retargetInterval", ecs_f32_t);
    float probe = ecs_const_var_get_t(world, "cfg.wallProbe", ecs_f32_t);
    float swerve = ecs_const_var_get_t(world, "cfg.critterJitter", ecs_f32_t);

    for (int i = 0; i < it->count; i ++) {
        vel[i].x = 0;
        vel[i].z = 0;

        if (health[i].value <= 0) {
            continue;
        }

        ecs_entity_t target = target_field[i].value;
        const FlecsPosition3 *tpos = NULL;

        if (target && ecs_is_alive(world, target)) {
            const OreHealth *th = ecs_get(world, target, OreHealth);
            tpos = ecs_get(world, target, FlecsPosition3);

            if (!th || th->value <= 0) {
                tpos = NULL;
            }
        }

        if (!tpos) {
            target = 0;
            target_field[i].value = 0;
        }

        critter[i].retarget -= it->delta_time;

        if (!target || critter[i].retarget <= 0) {
            critter[i].retarget = retarget * (0.75f + oreRandf() * 0.5f);

            ecs_entity_t next = oreCritterAcquire(
                world, game, combat, rocket, pos[i].x, pos[i].z, aggro);

            if (next && next != target) {
                target = next;
                target_field[i].value = next;
                tpos = ecs_get(world, next, FlecsPosition3);
            }
        }

        float gx = tpos ? tpos->x : 0;
        float gz = tpos ? tpos->z : 0;

        float dx = gx - pos[i].x;
        float dz = gz - pos[i].z;
        float dist = sqrtf(dx * dx + dz * dz);

        critter[i].attacking = target != 0 && dist <= range[i].value;

        if (critter[i].attacking) {
            if (ranged) {
                timer[i].value -= it->delta_time;

                if (timer[i].value <= 0) {
                    timer[i].value = interval[i].value;

                    const OreProjectile *shot = ecs_get(
                        world, ammo[i].projectile, OreProjectile);

                    float len = dist > 0.001f ? dist : 1.0f;
                    float sp = muzzle[i].speed > 0 ? muzzle[i].speed : 1.0f;
                    float lift = shot
                        ? 0.5f * shot->gravity * (len / sp)
                        : 0;

                    oreSpawnProjectile(world, game, ammo[i].projectile,
                        pos[i].x, pos[i].y + muzzle[i].height, pos[i].z,
                        dx / len * sp, lift, dz / len * sp,
                        dmg[i].value, NULL, false, target);
                }
            } else if (target == game->player) {
                oreDamagePlayer(world, game, arsenal, rocket,
                    dmg[i].value * it->delta_time);
            } else {
                oreDamageBuilding(world, game, arsenal, target,
                    dmg[i].value * it->delta_time);
            }
        }

        if (dist > 0.001f) {
            float ndx = dx / dist;
            float ndz = dz / dist;
            float a = critter[i].jitter * swerve;
            float ca = cosf(a), sa = sinf(a);
            float rdx = ndx * ca - ndz * sa;
            float rdz = ndx * sa + ndz * ca;

            rot[i].y = atan2f(rdx, rdz);

            if (!critter[i].attacking) {
                int32_t col = oreColAt(&map, pos[i].x + rdx * probe);
                int32_t row = oreRowAt(&map, pos[i].z + rdz * probe);

                if (oreOnGrid(row, col)) {
                    ecs_entity_t block = *oreBuildingCell(game, row, col);
                    if (block && block != target) {
                        target_field[i].value = block;
                        critter[i].retarget = retarget;
                    }
                }

                vel[i].x = rdx * speed[i].value;
                vel[i].z = rdz * speed[i].value;

                pos[i].x += rdx * speed[i].value * it->delta_time;
                pos[i].z += rdz * speed[i].value * it->delta_time;

                critter[i].anim += it->delta_time * speed[i].value *
                    critter[i].step;
            }
        }

        pos[i].y = critter[i].attacking
            ? 0
            : fabsf(sinf(critter[i].anim)) * critter[i].bob;

        rot[i].z = sinf(critter[i].anim * 0.5f) * 0.07f;
    }
}

static bool oreCrowdReserve(OreCrowd *crowd, int32_t count, int32_t cells) {
    if (count > crowd->cap) {
        int32_t cap = crowd->cap ? crowd->cap : 128;
        while (cap < count) {
            cap *= 2;
        }

        ecs_os_free(crowd->x);
        ecs_os_free(crowd->z);
        ecs_os_free(crowd->r);
        ecs_os_free(crowd->px);
        ecs_os_free(crowd->pz);
        ecs_os_free(crowd->cell);
        ecs_os_free(crowd->order);

        crowd->x = ecs_os_malloc_n(float, cap);
        crowd->z = ecs_os_malloc_n(float, cap);
        crowd->r = ecs_os_malloc_n(float, cap);
        crowd->px = ecs_os_malloc_n(float, cap);
        crowd->pz = ecs_os_malloc_n(float, cap);
        crowd->cell = ecs_os_malloc_n(int32_t, cap);
        crowd->order = ecs_os_malloc_n(int32_t, cap);
        crowd->cap = cap;
    }

    if (cells + 1 > crowd->cells) {
        ecs_os_free(crowd->start);
        crowd->start = ecs_os_malloc_n(int32_t, cells + 1);
        crowd->cells = cells + 1;
    }

    return crowd->x != NULL && crowd->start != NULL;
}

static void OreSeparation(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    OreCombatState *combat = ecs_field(it, OreCombatState, 0);

    float size = ecs_const_var_get_t(world, "cfg.crowdCell", ecs_f32_t);
    float push = ecs_const_var_get_t(world, "cfg.crowdPush", ecs_f32_t);
    float top = ecs_const_var_get_t(world, "cfg.crowdMaxSpeed", ecs_f32_t);
    int32_t max_near = ecs_const_var_get_t(
        world, "cfg.crowdNeighbors", ecs_i32_t);
    int32_t bucket = ecs_const_var_get_t(world, "cfg.crowdBucket", ecs_i32_t);

    if (size <= 0 || push <= 0 || max_near <= 0 || bucket <= 0) {
        return;
    }

    int32_t dim = (int32_t)(ORE_CROWD_SPAN / size) + 1;
    int32_t cells = dim * dim;

    OreCrowd *crowd = combat->crowd;
    if (!crowd) {
        crowd = ecs_os_calloc_t(OreCrowd);
        combat->crowd = crowd;
    }

    int32_t n = 0;

    ecs_iter_t cit = ecs_query_iter(world, combat->crowd_query);
    while (ecs_query_next(&cit)) {
        n += cit.count;
    }

    if (n < 2) {
        return;
    }

    if (!oreCrowdReserve(crowd, n, cells)) {
        return;
    }

    crowd->dim = dim;

    float *x = crowd->x, *z = crowd->z, *r = crowd->r;
    float *px = crowd->px, *pz = crowd->pz;
    int32_t *cell = crowd->cell, *order = crowd->order, *start = crowd->start;

    int32_t k = 0;

    cit = ecs_query_iter(world, combat->crowd_query);
    while (ecs_query_next(&cit)) {
        const OreRadius *radius = ecs_field(&cit, OreRadius, 1);
        const FlecsPosition3 *pos = ecs_field(&cit, FlecsPosition3, 2);

        for (int i = 0; i < cit.count && k < n; i ++) {
            x[k] = pos[i].x;
            z[k] = pos[i].z;
            r[k] = radius[i].value;
            k ++;
        }
    }

    n = k;

    ecs_os_memset(start, 0, (ecs_size_t)(cells + 1) * ECS_SIZEOF(int32_t));

    for (int32_t i = 0; i < n; i ++) {
        int32_t cx = (int32_t)((x[i] - ORE_CROWD_ORIGIN) / size);
        int32_t cz = (int32_t)((z[i] - ORE_CROWD_ORIGIN) / size);

        if (cx < 0) cx = 0;
        if (cx >= dim) cx = dim - 1;
        if (cz < 0) cz = 0;
        if (cz >= dim) cz = dim - 1;

        cell[i] = cz * dim + cx;
        start[cell[i] + 1] ++;
    }

    for (int32_t c = 0; c < cells; c ++) {
        start[c + 1] += start[c];
    }

    for (int32_t i = 0; i < n; i ++) {
        order[start[cell[i]] ++] = i;
    }

    for (int32_t c = cells; c > 0; c --) {
        start[c] = start[c - 1];
    }

    start[0] = 0;

    float step = top * it->delta_time;
    float gain = push * it->delta_time;

    if (max_near > ORE_CROWD_NEAR) {
        max_near = ORE_CROWD_NEAR;
    }

    for (int32_t i = 0; i < n; i ++) {
        px[i] = 0;
        pz[i] = 0;
    }

    for (int32_t i = 0; i < n; i ++) {
        int32_t near_j[ORE_CROWD_NEAR];
        float near_d[ORE_CROWD_NEAR];
        float near_x[ORE_CROWD_NEAR];
        float near_z[ORE_CROWD_NEAR];

        int32_t got = 0;
        int32_t weak = 0;
        int32_t cx = cell[i] % dim;
        int32_t cz = cell[i] / dim;
        float mi = r[i] * r[i];

        int32_t reach = (int32_t)ceilf(2.0f * r[i] / size);
        if (reach < 1) {
            reach = 1;
        }

        for (int32_t oz = -reach; oz <= reach; oz ++) {
            int32_t nz = cz + oz;
            if (nz < 0 || nz >= dim) {
                continue;
            }

            for (int32_t ox = -reach; ox <= reach; ox ++) {
                int32_t nx = cx + ox;
                if (nx < 0 || nx >= dim) {
                    continue;
                }

                int32_t c = nz * dim + nx;
                int32_t b = start[c];
                int32_t e = start[c + 1];

                if (e - b > bucket) {
                    e = b + bucket;
                }

                for (; b < e; b ++) {
                    int32_t j = order[b];
                    if (j == i) {
                        continue;
                    }

                    float dx = x[i] - x[j];
                    float dz = z[i] - z[j];
                    float rr = r[i] + r[j];
                    float d2 = dx * dx + dz * dz;

                    if (d2 >= rr * rr) {
                        continue;
                    }

                    float d = sqrtf(d2);
                    float over = rr - d;

                    if (got == max_near && over <= near_d[weak]) {
                        continue;
                    }

                    if (d > 0.0001f) {
                        dx /= d;
                        dz /= d;
                    } else {
                        float a = (float)((i * 7919 + j * 104729) % 629)
                            * 0.01f;
                        dx = cosf(a);
                        dz = sinf(a);
                    }

                    int32_t slot = got;
                    if (got < max_near) {
                        got ++;
                    } else {
                        slot = weak;
                    }

                    near_j[slot] = j;
                    near_d[slot] = over;
                    near_x[slot] = dx;
                    near_z[slot] = dz;

                    weak = 0;
                    for (int32_t q = 1; q < got; q ++) {
                        if (near_d[q] < near_d[weak]) {
                            weak = q;
                        }
                    }
                }
            }
        }

        for (int32_t q = 0; q < got; q ++) {
            int32_t j = near_j[q];
            float mj = r[j] * r[j];
            float sum = mi + mj;
            float wi = sum > 0 ? mj / sum : 0.5f;
            float wj = sum > 0 ? mi / sum : 0.5f;
            float over = near_d[q];

            px[i] += near_x[q] * over * wi;
            pz[i] += near_z[q] * over * wi;
            px[j] -= near_x[q] * over * wj;
            pz[j] -= near_z[q] * over * wj;
        }
    }

    for (int32_t i = 0; i < n; i ++) {
        float sx = px[i] * gain;
        float sz = pz[i] * gain;

        float len = sqrtf(sx * sx + sz * sz);
        if (len > step && len > 0) {
            sx *= step / len;
            sz *= step / len;
        }

        px[i] = sx;
        pz[i] = sz;
    }

    k = 0;

    cit = ecs_query_iter(world, combat->crowd_query);
    while (ecs_query_next(&cit)) {
        FlecsPosition3 *pos = ecs_field(&cit, FlecsPosition3, 2);

        for (int i = 0; i < cit.count && k < n; i ++) {
            pos[i].x += px[k];
            pos[i].z += pz[k];
            k ++;
        }
    }
}

static void oreTurretMuzzle(
    ecs_world_t *world,
    const OreTurret *turret,
    const FlecsPosition3 *pos,
    const OreMuzzle *muzzle,
    float *out)
{
    float s = sinf(turret->aim);
    float c = cosf(turret->aim);

    const FlecsPosition3 *head = turret->head ?
        ecs_get(world, turret->head, FlecsPosition3) : NULL;
    const FlecsPosition3 *tip = turret->fx ?
        ecs_get(world, turret->fx, FlecsPosition3) : NULL;

    if (head && tip) {
        out[0] = pos->x + head->x + tip->x * c + tip->z * s;
        out[1] = pos->y + head->y + tip->y;
        out[2] = pos->z + head->z - tip->x * s + tip->z * c;
    } else {
        out[0] = pos->x + s * muzzle->forward;
        out[1] = pos->y + muzzle->height;
        out[2] = pos->z + c * muzzle->forward;
    }
}

static void oreBeamRotation(float dx, float dy, float dz, float *out) {
    float h = sqrtf(dy * dy + dz * dz);

    out[0] = atan2f(-dy, dz);
    out[1] = atan2f(dx, h);
    out[2] = 0;
}

void oreFxToggle(ecs_world_t *world, ecs_entity_t fx, bool on) {
    if (!fx || !ecs_is_alive(world, fx)) {
        return;
    }

    const OreFlashLight *flash = ecs_get(world, fx, OreFlashLight);
    const FlecsPointLight *light = ecs_get(world, fx, FlecsPointLight);

    if (flash && light) {
        float want = on ? flash->peak : 0;

        if (light->intensity != want) {
            ecs_set(world, fx, FlecsPointLight, {
                .intensity = want,
                .range = light->range
            });
        }
    }

    FlecsParticleEmitter *emitter = ecs_get_mut(
        world, fx, FlecsParticleEmitter);
    if (emitter && emitter->enabled != on) {
        emitter->enabled = on;
        ecs_modified(world, fx, FlecsParticleEmitter);
    }
}

static void oreLaunchFx(
    ecs_world_t *world,
    const OreGame *game,
    ecs_entity_t root,
    float aim,
    float x,
    float y,
    float z)
{
    if (!root) {
        return;
    }

    float at[3] = {x, y, z};
    float s = sinf(aim);
    float c = cosf(aim);

    ecs_iter_t it = ecs_children(world, root);
    while (ecs_children_next(&it)) {
        for (int i = 0; i < it.count; i ++) {
            const FlecsParticleBurst *b = ecs_get(
                world, it.entities[i], FlecsParticleBurst);
            if (!b) {
                continue;
            }

            FlecsParticleBurst turned = *b;

            turned.velocity.x = b->velocity.x * c + b->velocity.z * s;
            turned.velocity.z = b->velocity.z * c - b->velocity.x * s;
            turned.velocity_variance.x = fabsf(
                b->velocity_variance.x * c + b->velocity_variance.z * s);
            turned.velocity_variance.z = fabsf(
                b->velocity_variance.z * c - b->velocity_variance.x * s);
            turned.offset.x = b->offset.x * c + b->offset.z * s;
            turned.offset.z = b->offset.z * c - b->offset.x * s;

            ecs_entity_t pool = b->pool ? b->pool : game->fx_pool;
            flecsEngine_particlesBurst(world, pool, at, &turned);
        }
    }
}

static void oreTurretFlash(ecs_world_t *world, ecs_entity_t fx) {
    if (!fx) {
        return;
    }

    OreFlashLight *flash = ecs_get_mut(world, fx, OreFlashLight);
    if (!flash) {
        return;
    }

    flash->level = flash->peak;
    ecs_modified(world, fx, OreFlashLight);
}

static void OreFlashLights(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    OreFlashLight *flash = ecs_field(it, OreFlashLight, 0);
    const FlecsPointLight *light = ecs_field(it, FlecsPointLight, 1);

    bool light_self = ecs_field_is_self(it, 1);

    for (int i = 0; i < it->count; i ++) {
        if (flash[i].level <= 0) {
            continue;
        }

        flash[i].level -= flash[i].fade * it->delta_time;
        if (flash[i].level < 0) {
            flash[i].level = 0;
        }

        ecs_entity_t e = it->entities[i];

        if (flash[i].once && flash[i].level <= 0) {
            ecs_delete(world, e);
            continue;
        }

        if (light_self && light[i].intensity == flash[i].level) {
            continue;
        }

        FlecsPointLight *dst = ecs_ensure(world, e, FlecsPointLight);
        dst->intensity = flash[i].level;
        ecs_modified(world, e, FlecsPointLight);
    }
}

static void OreTurrets(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    OreTurret *turret = ecs_field(it, OreTurret, 0);
    const OreDamage *dmg = ecs_field(it, OreDamage, 1);
    const OreRange *range = ecs_field(it, OreRange, 2);
    OreTarget *target_field = ecs_field(it, OreTarget, 3);
    const OreMuzzle *muzzle = ecs_field(it, OreMuzzle, 4);
    const OreAttackInterval *interval = ecs_field(it, OreAttackInterval, 5);
    OreAttackTimer *timer = ecs_field(it, OreAttackTimer, 6);
    const OreAmmo *ammo = ecs_field(it, OreAmmo, 7);
    const OreBeam *beam = ecs_field(it, OreBeam, 8);
    const OreSplash *splash = ecs_field(it, OreSplash, 9);
    const OrePowerConsumer *power = ecs_field(it, OrePowerConsumer, 10);
    const FlecsPosition3 *pos = ecs_field(it, FlecsPosition3, 11);
    OreGame *game = ecs_field(it, OreGame, 12);
    const OreArsenal *arsenal = ecs_field(it, OreArsenal, 13);
    OreWaveState *waves = ecs_field(it, OreWaveState, 14);
    const OreCombatState *combat = ecs_field(it, OreCombatState, 15);
    const OrePowerState *power_state = ecs_field(it, OrePowerState, 16);

    bool ranged = interval && timer && ammo;
    bool dark = power && power_state->blackout;

    float aim_y = ecs_const_var_get_t(world, "cfg.turretAimY", ecs_f32_t);
    float aim_per_radius = ecs_const_var_get_t(
        world, "cfg.turretAimPerRadius", ecs_f32_t);
    float hit_inset = ecs_const_var_get_t(
        world, "cfg.turretHitInset", ecs_f32_t);

    for (int i = 0; i < it->count; i ++) {
        if (!turret[i].head) {
            turret[i].head = oreChild(world, it->entities[i], "head");
        }

        if (!turret[i].fx && turret[i].head) {
            turret[i].fx = oreChild(world, turret[i].head, "fx");
        }

        if (beam && !turret[i].hit_fx) {
            turret[i].hit_fx = oreChild(world, it->entities[i], "hit_fx");
        }

        float mul = orePowerMul(world, it->entities[i]);

        if (dark || mul <= 0) {
            oreFxToggle(world, turret[i].fx, false);
            oreFxToggle(world, turret[i].hit_fx, false);
            continue;
        }

        ecs_entity_t target = target_field[i].value;
        const FlecsPosition3 *tpos = NULL;

        if (target && ecs_is_alive(world, target)) {
            const OreHealth *th = ecs_get(world, target, OreHealth);
            tpos = ecs_get(world, target, FlecsPosition3);

            if (!th || th->value <= 0 || !tpos) {
                tpos = NULL;
            } else {
                float dx = tpos->x - pos[i].x;
                float dz = tpos->z - pos[i].z;
                if (dx * dx + dz * dz > range[i].value * range[i].value) {
                    tpos = NULL;
                }
            }
        }

        if (!tpos) {
            target = oreNearestCritter(
                world, combat, pos[i].x, pos[i].z, range[i].value);
            target_field[i].value = target;
            tpos = target ? ecs_get(world, target, FlecsPosition3) : NULL;
        }

        if (!tpos) {
            oreFxToggle(world, turret[i].fx, false);
            oreFxToggle(world, turret[i].hit_fx, false);
            continue;
        }

        float lx = tpos->x;
        float lz = tpos->z;

        if (!beam && ranged && !ecs_has(world, ammo[i].projectile, OreHoming)) {
            const OreVelocity *tv = ecs_get(world, target, OreVelocity);
            float sp = muzzle[i].speed > 0 ? muzzle[i].speed : 1.0f;

            if (tv && (tv->x != 0 || tv->z != 0)) {
                float ay = tpos->y + aim_y - (pos[i].y + muzzle[i].height);
                float ax = tpos->x - pos[i].x;
                float az = tpos->z - pos[i].z;
                float t = sqrtf(ax * ax + ay * ay + az * az) / sp;

                ax = tpos->x + tv->x * t - pos[i].x;
                az = tpos->z + tv->z * t - pos[i].z;
                t = sqrtf(ax * ax + ay * ay + az * az) / sp;

                if (t > 2.0f) {
                    t = 2.0f;
                }

                lx = tpos->x + tv->x * t;
                lz = tpos->z + tv->z * t;
            }
        }

        float dx = lx - pos[i].x;
        float dz = lz - pos[i].z;

        turret[i].aim = atan2f(dx, dz);

        if (turret[i].head) {
            ecs_set(world, turret[i].head, FlecsRotation3,
                {0, turret[i].aim, 0});
        }

        float mx, my, mz;

        if (beam) {
            float m[3];
            oreTurretMuzzle(world, &turret[i], &pos[i], &muzzle[i], m);
            mx = m[0];
            my = m[1];
            mz = m[2];
        } else {
            mx = pos[i].x + sinf(turret[i].aim) * muzzle[i].forward;
            my = pos[i].y + muzzle[i].height;
            mz = pos[i].z + cosf(turret[i].aim) * muzzle[i].forward;
        }

        if (beam) {
            oreDamageCritter(world, game, arsenal, combat, waves, target,
                dmg[i].value * it->delta_time * mul);

            const OreRadius *trad = ecs_get(world, target, OreRadius);
            float tr = trad && trad->value > 0 ? trad->value : 0;

            float bx = tpos->x - mx;
            float by = tpos->y + (tr > 0 ? tr * aim_per_radius : aim_y) - my;
            float bz = tpos->z - mz;
            float len = sqrtf(bx * bx + by * by + bz * bz);

            float rot[3];
            oreBeamRotation(bx, by, bz, rot);

            flecsEngine_draw(world, beam[i].prefab,
                &(flecs_draw_instance_t){
                    .position = {
                        mx + bx * 0.5f, my + by * 0.5f, mz + bz * 0.5f},
                    .rotation = {rot[0], rot[1], rot[2]},
                    .scale = {1, 1, len}
                }, 1);

            oreFxToggle(world, turret[i].fx, true);

            if (turret[i].hit_fx) {
                float inset = (tr > 0 ? tr : 0.5f) * hit_inset;
                float back = len > 0.001f ? inset / len : 0;

                if (back > 0.5f) {
                    back = 0.5f;
                }

                ecs_set(world, turret[i].hit_fx, FlecsPosition3, {
                    mx + bx * (1.0f - back) - pos[i].x,
                    my + by * (1.0f - back) - pos[i].y,
                    mz + bz * (1.0f - back) - pos[i].z
                });

                oreFxToggle(world, turret[i].hit_fx, true);
            }

            continue;
        }

        if (!ranged) {
            continue;
        }

        timer[i].value -= it->delta_time;

        if (timer[i].value > 0) {
            continue;
        }

        timer[i].value = interval[i].value / mul;

        float sx = lx - mx;
        float sy = tpos->y + aim_y - my;
        float sz = lz - mz;
        float len = sqrtf(sx * sx + sy * sy + sz * sz);
        if (len < 0.001f) {
            len = 1.0f;
        }

        float sp = muzzle[i].speed;

        oreSpawnProjectile(world, game, ammo[i].projectile, mx, my, mz,
            sx / len * sp, sy / len * sp, sz / len * sp,
            dmg[i].value, splash ? &splash[i] : NULL, true, target);

        oreBurst(world, game->glow_pool, arsenal->spark_burst, mx, my, mz);

        oreTurretFlash(world, turret[i].fx);

        const OreLaunchFx *launch = ecs_get(
            world, it->entities[i], OreLaunchFx);
        if (launch) {
            oreLaunchFx(world, game, launch->burst, turret[i].aim,
                mx, my, mz);
        }
    }
}

static const FlecsPosition3* oreLiveTargetPos(
    ecs_world_t *world,
    ecs_entity_t target)
{
    if (!target || !ecs_is_alive(world, target)) {
        return NULL;
    }

    const OreHealth *health = ecs_get(world, target, OreHealth);
    if (!health || health->value <= 0) {
        return NULL;
    }

    return ecs_get(world, target, FlecsPosition3);
}

static void oreHomingSteer(
    ecs_world_t *world,
    const OreCombatState *combat,
    const OreHoming *homing,
    OreTarget *target,
    OreProjectile *shot,
    const FlecsPosition3 *pos,
    FlecsRotation3 *rot,
    float aim_y,
    float dt)
{
    float sp = sqrtf(shot->vx * shot->vx + shot->vy * shot->vy +
        shot->vz * shot->vz);
    if (sp < 0.0001f) {
        return;
    }

    const FlecsPosition3 *tpos = oreLiveTargetPos(world, target->value);

    if (!tpos && homing->reacquire > 0) {
        ecs_entity_t next = oreNearestCritter(
            world, combat, pos->x, pos->z, homing->reacquire);

        target->value = next;
        tpos = oreLiveTargetPos(world, next);
    }

    if (tpos) {
        float dx = tpos->x - pos->x;
        float dy = tpos->y + aim_y - pos->y;
        float dz = tpos->z - pos->z;
        float dist = sqrtf(dx * dx + dy * dy + dz * dz);

        if (dist > 0.0001f) {
            float tx = dx / dist, ty = dy / dist, tz = dz / dist;
            float hx = shot->vx / sp, hy = shot->vy / sp, hz = shot->vz / sp;
            float lock = homing->turn_rate > 0 ? sp / homing->turn_rate : 0;

            if (homing->turn_rate > 0 && dist > lock) {
                float turn = homing->turn_rate * dt;
                float dot = hx * tx + hy * ty + hz * tz;

                if (dot > 1.0f) dot = 1.0f;
                if (dot < -1.0f) dot = -1.0f;

                if (acosf(dot) > turn) {
                    float ox = tx - hx * dot;
                    float oy = ty - hy * dot;
                    float oz = tz - hz * dot;
                    float olen = sqrtf(ox * ox + oy * oy + oz * oz);

                    if (olen > 0.0001f) {
                        float c = cosf(turn), s = sinf(turn) / olen;

                        tx = hx * c + ox * s;
                        ty = hy * c + oy * s;
                        tz = hz * c + oz * s;
                    }
                }
            }

            shot->vx = tx * sp;
            shot->vy = ty * sp;
            shot->vz = tz * sp;
        }
    }

    if (rot) {
        float flat = sqrtf(shot->vy * shot->vy + shot->vz * shot->vz);

        rot->x = atan2f(-shot->vy, shot->vz);
        rot->y = atan2f(shot->vx, flat);
        rot->z = 0;
    }
}

static void OreProjectiles(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    OreProjectile *shot = ecs_field(it, OreProjectile, 0);
    const OreDamage *dmg = ecs_field(it, OreDamage, 1);
    const OreSplash *splash = ecs_field(it, OreSplash, 2);
    const OreHoming *homing = ecs_field(it, OreHoming, 3);
    OreTarget *target = ecs_field(it, OreTarget, 4);
    FlecsPosition3 *pos = ecs_field(it, FlecsPosition3, 5);
    FlecsRotation3 *rot = ecs_field(it, FlecsRotation3, 6);
    OreGame *game = ecs_field(it, OreGame, 7);
    const OreArsenal *arsenal = ecs_field(it, OreArsenal, 8);
    OreWaveState *waves = ecs_field(it, OreWaveState, 9);
    const OreCombatState *combat = ecs_field(it, OreCombatState, 10);

    const OreRocketState *rocket = ecs_singleton_get(world, OreRocketState);

    float aim_y = ecs_const_var_get_t(world, "cfg.turretAimY", ecs_f32_t);

    for (int i = 0; i < it->count; i ++) {
        if (homing && target) {
            oreHomingSteer(world, combat, &homing[i], &target[i], &shot[i],
                &pos[i], rot ? &rot[i] : NULL, aim_y, it->delta_time);
        } else {
            shot[i].vy -= shot[i].gravity * it->delta_time;
        }

        pos[i].x += shot[i].vx * it->delta_time;
        pos[i].y += shot[i].vy * it->delta_time;
        pos[i].z += shot[i].vz * it->delta_time;

        shot[i].life -= it->delta_time;

        ecs_entity_t hit = 0;

        if (shot[i].hits_critters) {
            hit = oreNearestCritter(
                world, combat, pos[i].x, pos[i].z, shot[i].radius);
        } else {
            float r2 = shot[i].radius * shot[i].radius;

            if (orePlayerWithin(world, game, rocket, pos[i].x, pos[i].z, &r2)) {
                hit = game->player;
            } else {
                hit = oreNearest(world, combat->target_query,
                    pos[i].x, pos[i].z, &r2);
            }
        }

        bool expired = shot[i].life <= 0 || pos[i].y <= 0;

        if (!hit && !expired) {
            continue;
        }

        if (splash) {
            float blast = splash[i].radius / 5.0f;
            if (blast < 0.65f) blast = 0.65f;
            if (blast > 1.25f) blast = 1.25f;

            oreExplosion(world, game, arsenal->boom_burst,
                pos[i].x, pos[i].y + 0.2f, pos[i].z, blast);

            float r2 = splash[i].radius * splash[i].radius;

            ecs_iter_t sit = ecs_query_iter(world, combat->critter_query);
            while (ecs_query_next(&sit)) {
                const OreHealth *health = ecs_field(&sit, OreHealth, 1);
                const FlecsPosition3 *apos = ecs_field(
                    &sit, FlecsPosition3, 2);

                for (int j = 0; j < sit.count; j ++) {
                    if (health[j].value <= 0) {
                        continue;
                    }

                    float dx = apos[j].x - pos[i].x;
                    float dz = apos[j].z - pos[i].z;
                    if (dx * dx + dz * dz > r2) {
                        continue;
                    }

                    oreDamageCritter(world, game, arsenal, combat, waves,
                        sit.entities[j], dmg[i].value);
                }
            }
        } else if (hit) {
            oreBurst(world, game->glow_pool, arsenal->spark_burst,
                pos[i].x, pos[i].y, pos[i].z);
            oreBurst(world, game->fx_pool, arsenal->hit_puff,
                pos[i].x, pos[i].y, pos[i].z);

            if (shot[i].hits_critters) {
                oreDamageCritter(world, game, arsenal, combat, waves, hit,
                    dmg[i].value);
            } else if (hit == game->player) {
                oreDamagePlayer(world, game, arsenal, rocket, dmg[i].value);
            } else {
                oreDamageBuilding(world, game, arsenal, hit, dmg[i].value);
            }
        } else if (pos[i].y <= 0.05f) {
            oreBurst(world, game->fx_pool, arsenal->impact_dust,
                pos[i].x, 0.05f, pos[i].z);
            oreBurst(world, game->glow_pool, arsenal->spark_burst,
                pos[i].x, 0.05f, pos[i].z);
        }

        ecs_delete(world, it->entities[i]);
    }
}

static void OrePlayerCombat(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    OrePlayer *player = ecs_field(it, OrePlayer, 0);
    OreHealth *health = ecs_field(it, OreHealth, 1);
    const OreDamage *dmg = ecs_field(it, OreDamage, 2);
    const OreRange *range = ecs_field(it, OreRange, 3);
    const OreMuzzle *muzzle = ecs_field(it, OreMuzzle, 4);
    const OreAttackInterval *interval = ecs_field(it, OreAttackInterval, 5);
    OreAttackTimer *timer = ecs_field(it, OreAttackTimer, 6);
    const OreAmmo *ammo = ecs_field(it, OreAmmo, 7);
    const FlecsPosition3 *pos = ecs_field(it, FlecsPosition3, 8);
    OreGame *game = ecs_field(it, OreGame, 9);
    const OreCombatState *combat = ecs_field(it, OreCombatState, 10);

    const OreRocketState *rocket = ecs_singleton_get(world, OreRocketState);

    float aim_y = ecs_const_var_get_t(world, "cfg.turretAimY", ecs_f32_t);
    float regen = ecs_const_var_get_t(world, "cfg.playerRegen", ecs_f32_t);
    float regen_delay = ecs_const_var_get_t(
        world, "cfg.playerRegenDelay", ecs_f32_t);

    for (int i = 0; i < it->count; i ++) {
        if (health[i].value <= 0 || (rocket && rocket->boarded)) {
            continue;
        }

        player[i].hurt += it->delta_time;

        if (player[i].hurt >= regen_delay &&
            health[i].value < health[i].max)
        {
            health[i].value += regen * it->delta_time;
            if (health[i].value > health[i].max) {
                health[i].value = health[i].max;
            }
        }

        timer[i].value -= it->delta_time;

        if (timer[i].value > 0) {
            continue;
        }

        timer[i].value = 0;

        ecs_entity_t target = oreNearestCritter(
            world, combat, pos[i].x, pos[i].z, range[i].value);
        if (!target) {
            continue;
        }

        const FlecsPosition3 *tpos = ecs_get(world, target, FlecsPosition3);
        if (!tpos) {
            continue;
        }

        timer[i].value = interval[i].value;

        float sp = muzzle[i].speed;

        float dx = tpos->x - pos[i].x;
        float dy = tpos->y + aim_y - (pos[i].y + muzzle[i].height);
        float dz = tpos->z - pos[i].z;
        float len = sqrtf(dx * dx + dy * dy + dz * dz);
        if (len < 0.001f) {
            len = 1.0f;
        }

        oreSpawnProjectile(world, game, ammo[i].projectile,
            pos[i].x, pos[i].y + muzzle[i].height, pos[i].z,
            dx / len * sp, dy / len * sp, dz / len * sp,
            dmg[i].value, NULL, true, target);
    }
}

void oreCombatImport(ecs_world_t *world) {
    const char *seed = getenv("ORE_SEED");
    srand(seed ? (unsigned int)strtoul(seed, NULL, 10)
        : (unsigned int)time(NULL));

    ECS_META_COMPONENT(world, OreHealth);
    ECS_META_COMPONENT(world, OreSpeed);
    ECS_META_COMPONENT(world, OreVelocity);
    ECS_META_COMPONENT(world, OreDamage);
    ECS_META_COMPONENT(world, OreRange);
    ECS_META_COMPONENT(world, OreAttackInterval);
    ECS_META_COMPONENT(world, OreAttackTimer);
    ECS_META_COMPONENT(world, OreSplash);
    ECS_META_COMPONENT(world, OreMuzzle);
    ECS_META_COMPONENT(world, OreAmmo);
    ECS_META_COMPONENT(world, OreBeam);
    ECS_META_COMPONENT(world, OreTarget);
    ECS_META_COMPONENT(world, OreCritter);
    ECS_META_COMPONENT(world, OreRadius);
    ECS_META_COMPONENT(world, OreCorpse);
    ECS_META_COMPONENT(world, OreProjectile);
    ECS_META_COMPONENT(world, OreHoming);
    ECS_META_COMPONENT(world, OreTurret);
    ECS_META_COMPONENT(world, OreLaunchFx);
    ECS_META_COMPONENT(world, OreDeathBurst);
    ECS_META_COMPONENT(world, OreFlashLight);
    ECS_META_COMPONENT(world, OreArsenal);
    ECS_META_COMPONENT(world, OreWaveMix);
    ECS_META_COMPONENT(world, OreWaveGate);
    ECS_META_COMPONENT(world, OreWaveStyle);
    ECS_META_COMPONENT(world, OreWaveState);
    ECS_META_COMPONENT(world, OreCombatState);

    ecs_add_id(world, ecs_id(OreArsenal), EcsSingleton);
    ecs_add_id(world, ecs_id(OreWaveState), EcsSingleton);
    ecs_add_id(world, ecs_id(OreCombatState), EcsSingleton);

    ecs_add_pair(world, ecs_id(OreAttackInterval), EcsWith,
        ecs_id(OreAttackTimer));
    ecs_add_pair(world, ecs_id(OreCritter), EcsWith, ecs_id(OreTarget));
    ecs_add_pair(world, ecs_id(OreCritter), EcsWith, ecs_id(OreVelocity));
    ecs_add_pair(world, ecs_id(OreTurret), EcsWith, ecs_id(OreTarget));
    ecs_add_pair(world, ecs_id(OreHoming), EcsWith, ecs_id(OreTarget));
    ecs_add_pair(world, ecs_id(OreCritter), EcsWith, ecs_id(FlecsRotation3));
    ecs_add_pair(world, ecs_id(OreCritter), EcsWith, FlecsDynamicTransform);
    ecs_add_pair(world, ecs_id(OreProjectile), EcsWith,
        FlecsDynamicTransform);

    OreCombatState *combat = ecs_singleton_ensure(world, OreCombatState);

    combat->critter_query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "OreCritterQuery" }),
        .terms = {
            { .id = ecs_id(OreCritter), .inout = EcsInOutNone },
            { .id = ecs_id(OreHealth), .inout = EcsIn },
            { .id = ecs_id(FlecsPosition3), .inout = EcsIn }
        }
    });

    combat->crowd_query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "OreCrowdQuery" }),
        .terms = {
            { .id = ecs_id(OreCritter), .inout = EcsInOutNone },
            { .id = ecs_id(OreRadius), .inout = EcsIn },
            { .id = ecs_id(FlecsPosition3), .inout = EcsInOut }
        }
    });

    combat->corpse_query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "OreCorpseQuery" }),
        .terms = {
            { .id = ecs_id(OreCorpse), .inout = EcsIn }
        }
    });

    combat->target_query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "OreTargetQuery" }),
        .terms = {
            { .id = ecs_id(OreBuilding), .inout = EcsInOutNone },
            { .id = ecs_id(OreHealth), .inout = EcsIn },
            { .id = ecs_id(FlecsPosition3), .inout = EcsIn }
        }
    });

    ecs_singleton_modified(world, OreCombatState);

    ecs_entity_t playing = ecs_constant_to_entity(
        world, OreGameState, OreGameStatePlaying);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreWaves" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreGame) },
            { .id = ecs_id(OreArsenal), .inout = EcsIn },
            { .id = ecs_id(OreWaveState) },
            { .id = ecs_id(OreCombatState), .inout = EcsIn },
            { .id = ecs_pair_t(OreGameState, playing) }
        },
        .callback = OreWaves
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreCritters" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreCritter) },
            { .id = ecs_id(OreHealth) },
            { .id = ecs_id(OreSpeed) },
            { .id = ecs_id(OreVelocity) },
            { .id = ecs_id(OreDamage) },
            { .id = ecs_id(OreRange) },
            { .id = ecs_id(OreTarget) },
            { .id = ecs_id(OreAttackInterval), .oper = EcsOptional },
            { .id = ecs_id(OreAttackTimer), .oper = EcsOptional },
            { .id = ecs_id(OreMuzzle), .oper = EcsOptional },
            { .id = ecs_id(OreAmmo), .oper = EcsOptional },
            { .id = ecs_id(FlecsPosition3) },
            { .id = ecs_id(FlecsRotation3) },
            { .id = ecs_id(OreGame) },
            { .id = ecs_id(OreArsenal), .inout = EcsIn },
            { .id = ecs_id(OreCombatState), .inout = EcsIn },
            { .id = ecs_pair_t(OreGameState, playing) }
        },
        .callback = OreCritters
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreSeparation" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreCombatState) },
            { .id = ecs_pair_t(OreGameState, playing) }
        },
        .callback = OreSeparation
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreCorpses" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreCorpse) },
            { .id = ecs_id(FlecsPosition3) }
        },
        .callback = OreCorpses
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreIndexCritters" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreCombatState) },
            { .id = ecs_pair_t(OreGameState, playing) }
        },
        .callback = OreIndexCritters
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreTurrets" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreTurret) },
            { .id = ecs_id(OreDamage) },
            { .id = ecs_id(OreRange) },
            { .id = ecs_id(OreTarget) },
            { .id = ecs_id(OreMuzzle) },
            { .id = ecs_id(OreAttackInterval), .oper = EcsOptional },
            { .id = ecs_id(OreAttackTimer), .oper = EcsOptional },
            { .id = ecs_id(OreAmmo), .oper = EcsOptional },
            { .id = ecs_id(OreBeam), .oper = EcsOptional },
            { .id = ecs_id(OreSplash), .oper = EcsOptional },
            { .id = ecs_id(OrePowerConsumer), .src.id = EcsSelf,
              .oper = EcsOptional },
            { .id = ecs_id(FlecsPosition3) },
            { .id = ecs_id(OreGame) },
            { .id = ecs_id(OreArsenal), .inout = EcsIn },
            { .id = ecs_id(OreWaveState) },
            { .id = ecs_id(OreCombatState), .inout = EcsIn },
            { .id = ecs_id(OrePowerState), .inout = EcsIn },
            { .id = ecs_pair_t(OreGameState, playing) }
        },
        .callback = OreTurrets
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreFlashLights" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreFlashLight) },
            { .id = ecs_id(FlecsPointLight) }
        },
        .callback = OreFlashLights
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreProjectiles" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreProjectile) },
            { .id = ecs_id(OreDamage) },
            { .id = ecs_id(OreSplash), .oper = EcsOptional },
            { .id = ecs_id(OreHoming), .oper = EcsOptional },
            { .id = ecs_id(OreTarget), .oper = EcsOptional },
            { .id = ecs_id(FlecsPosition3) },
            { .id = ecs_id(FlecsRotation3), .oper = EcsOptional },
            { .id = ecs_id(OreGame) },
            { .id = ecs_id(OreArsenal), .inout = EcsIn },
            { .id = ecs_id(OreWaveState) },
            { .id = ecs_id(OreCombatState), .inout = EcsIn },
            { .id = ecs_pair_t(OreGameState, playing) }
        },
        .callback = OreProjectiles
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OrePlayerCombat" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OrePlayer) },
            { .id = ecs_id(OreHealth) },
            { .id = ecs_id(OreDamage) },
            { .id = ecs_id(OreRange) },
            { .id = ecs_id(OreMuzzle) },
            { .id = ecs_id(OreAttackInterval) },
            { .id = ecs_id(OreAttackTimer) },
            { .id = ecs_id(OreAmmo) },
            { .id = ecs_id(FlecsPosition3) },
            { .id = ecs_id(OreGame) },
            { .id = ecs_id(OreCombatState), .inout = EcsIn },
            { .id = ecs_pair_t(OreGameState, playing) }
        },
        .callback = OrePlayerCombat
    });
}
