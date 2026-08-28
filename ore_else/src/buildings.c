#define ORE_ELSE_BUILDINGS_IMPL
#include "ore_else.h"

ecs_entity_t* oreBuildingCell(OreGame *game, int32_t row, int32_t col) {
    return &game->building_grid[row * ORE_COLS + col];
}

void oreFootprint(
    ecs_world_t *world,
    ecs_entity_t prefab,
    int32_t *w,
    int32_t *h)
{
    const OreFootprint *fp = prefab
        ? ecs_get(world, prefab, OreFootprint)
        : NULL;

    *w = (fp && fp->w > 0) ? fp->w : 1;
    *h = (fp && fp->h > 0) ? fp->h : 1;
}

static void OreBuildingOnRemove(ecs_iter_t *it) {
    const OreBuilding *building = ecs_field(it, OreBuilding, 0);
    OreGame *game = ecs_field(it, OreGame, 1);

    for (int i = 0; i < it->count; i ++) {
        int32_t w, h;
        oreFootprint(it->world,
            ecs_get_target(it->world, it->entities[i], EcsIsA, 0), &w, &h);

        for (int r = building[i].row; r < building[i].row + h; r ++) {
            for (int c = building[i].col; c < building[i].col + w; c ++) {
                if (!oreOnGrid(r, c)) {
                    continue;
                }

                ecs_entity_t *cell = oreBuildingCell(game, r, c);
                if (*cell == it->entities[i]) {
                    *cell = 0;
                }

                oreRulesEvalCell(it->world, game, r, c);
            }
        }
    }
}

static void OreDrills(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    OreDrillState *drill = ecs_field(it, OreDrillState, 0);
    const OreBuilding *building = ecs_field(it, OreBuilding, 1);
    OreGame *game = ecs_field(it, OreGame, 2);
    const OrePowerState *power = ecs_field(it, OrePowerState, 3);

    float speed = ecs_const_var_get_t(world, "cfg.drillSpeed", ecs_f32_t);
    float rate = it->delta_time * speed;

    for (int i = 0; i < it->count; i ++) {
        drill[i].running = false;

        if (power->blackout) {
            continue;
        }

        float mul = orePowerMul(world, it->entities[i]);
        if (mul <= 0) {
            continue;
        }

        if (!oreOnGrid(building[i].row, building[i].col)) {
            continue;
        }

        ecs_entity_t deposit = *oreDepositCell(
            game, building[i].row, building[i].col);
        if (!deposit || !ecs_is_alive(world, deposit)) {
            drill[i].timer = 0;
            continue;
        }

        const OreDeposit *d = ecs_get(world, deposit, OreDeposit);
        const OreResource *resource = d
            ? ecs_get(world, d->resource, OreResource)
            : NULL;
        if (!resource) {
            continue;
        }

        drill[i].running = true;

        float interval = (float)resource->mine_time_ds * 0.1f;
        if (interval <= 0) {
            interval = 1.0f;
        }

        drill[i].timer += rate * mul;

        if (drill[i].timer < interval) {
            continue;
        }

        drill[i].timer -= interval;

        oreDepositMine(world, game, deposit);
    }
}

static ecs_entity_t oreOwner(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_id_t a,
    ecs_id_t b)
{
    while (e && !ecs_has_id(world, e, a) && !ecs_has_id(world, e, b)) {
        e = ecs_get_target(world, e, EcsChildOf, 0);
    }

    return e;
}

static ecs_entity_t oreWorkOwner(ecs_world_t *world, ecs_entity_t e) {
    return oreOwner(world, e, ecs_id(OreDrillState), ecs_id(OreCrafter));
}

static float oreWorkRate(
    ecs_world_t *world,
    const OrePowerState *power,
    ecs_entity_t owner)
{
    if (!owner) {
        return 1.0f;
    }

    if (power->blackout && ecs_has(world, owner, OrePowerConsumer)) {
        return 0;
    }

    const OreDrillState *drill = ecs_get(world, owner, OreDrillState);
    if (drill && !drill->running) {
        return 0;
    }

    const OreCrafter *crafter = ecs_get(world, owner, OreCrafter);
    if (crafter && !oreCrafterBusy(crafter)) {
        return 0;
    }

    return orePowerMul(world, owner);
}

static void OreSpinners(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    OreSpinner *spinner = ecs_field(it, OreSpinner, 0);
    FlecsRotation3 *rot = ecs_field(it, FlecsRotation3, 1);
    const OrePowerState *power = ecs_field(it, OrePowerState, 2);

    for (int i = 0; i < it->count; i ++) {
        if (!spinner[i].owner) {
            spinner[i].owner = oreWorkOwner(world, it->entities[i]);
        }

        float mul = oreWorkRate(world, power, spinner[i].owner);
        if (mul <= 0) {
            continue;
        }

        rot[i].y += spinner[i].speed * it->delta_time * mul;
    }
}

static void OrePistons(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    OrePiston *piston = ecs_field(it, OrePiston, 0);
    FlecsPosition3 *pos = ecs_field(it, FlecsPosition3, 1);
    const OreGame *game = ecs_field(it, OreGame, 2);
    const OrePowerState *power = ecs_field(it, OrePowerState, 3);

    for (int i = 0; i < it->count; i ++) {
        if (!piston[i].owner) {
            piston[i].owner = oreWorkOwner(world, it->entities[i]);
        }

        float mul = oreWorkRate(world, power, piston[i].owner);
        if (mul <= 0) {
            continue;
        }

        float was = piston[i].phase;
        piston[i].phase += piston[i].speed * it->delta_time * mul;

        float stroke = 0.5f - 0.5f * cosf(piston[i].phase);
        float at = piston[i].rest - piston[i].travel * stroke;

        if (piston[i].slide) {
            pos[i].z = at;
        } else {
            pos[i].y = at;
        }

        if (!piston[i].burst && !piston[i].spark) {
            continue;
        }

        float span = (float)(2.0 * M_PI);
        if (floorf((was - (float)M_PI) / span) ==
            floorf((piston[i].phase - (float)M_PI) / span))
        {
            continue;
        }

        const FlecsPosition3 *base = ecs_get(
            world, piston[i].owner, FlecsPosition3);
        if (!base) {
            continue;
        }

        float ix = base->x + pos[i].x;
        float iy = base->y + pos[i].y + piston[i].impact;
        float iz = base->z + pos[i].z;

        oreBurst(world, game->fx_pool, piston[i].burst, ix, iy, iz);
        oreBurst(world, game->glow_pool, piston[i].spark, ix, iy, iz);
    }
}

static void OreShakes(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    OreShake *shake = ecs_field(it, OreShake, 0);
    FlecsPosition3 *pos = ecs_field(it, FlecsPosition3, 1);
    const OrePowerState *power = ecs_field(it, OrePowerState, 2);

    for (int i = 0; i < it->count; i ++) {
        if (!shake[i].ready) {
            shake[i].owner = oreWorkOwner(world, it->entities[i]);
            shake[i].rest = pos[i].y;
            shake[i].ready = true;
        }

        float mul = oreWorkRate(world, power, shake[i].owner);
        if (mul <= 0) {
            pos[i].y = shake[i].rest;
            continue;
        }

        shake[i].phase += shake[i].frequency * (float)(2.0 * M_PI) *
            it->delta_time * mul;

        float wobble = 0.7f + 0.3f * sinf(shake[i].phase * 0.37f);

        pos[i].y = shake[i].rest +
            shake[i].amplitude * wobble * sinf(shake[i].phase);
    }
}

static void OreVents(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    OreVent *vent = ecs_field(it, OreVent, 0);
    const FlecsPosition3 *pos = ecs_field(it, FlecsPosition3, 1);
    const OreGame *game = ecs_field(it, OreGame, 2);
    const OrePowerState *power = ecs_field(it, OrePowerState, 3);

    for (int i = 0; i < it->count; i ++) {
        if (!vent[i].owner) {
            vent[i].owner = oreWorkOwner(world, it->entities[i]);
        }

        float mul = oreWorkRate(world, power, vent[i].owner);
        if (mul <= 0) {
            continue;
        }

        vent[i].timer -= it->delta_time * mul;

        if (vent[i].timer > 0) {
            continue;
        }

        vent[i].timer = vent[i].interval > 0 ? vent[i].interval : 0.5f;

        const FlecsPosition3 *base = ecs_get(
            world, vent[i].owner, FlecsPosition3);
        if (!base) {
            continue;
        }

        oreBurst(world, game->fx_pool, vent[i].burst,
            base->x + pos[i].x, base->y + pos[i].y, base->z + pos[i].z);
    }
}

static void OreWorkEmitters(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    OreWorkEmitter *work = ecs_field(it, OreWorkEmitter, 0);
    FlecsParticleEmitter *emitter = ecs_field(it, FlecsParticleEmitter, 1);
    const OrePowerState *power = ecs_field(it, OrePowerState, 2);

    for (int i = 0; i < it->count; i ++) {
        if (!work[i].owner) {
            work[i].owner = oreWorkOwner(world, it->entities[i]);
        }

        emitter[i].enabled = oreWorkRate(world, power, work[i].owner) > 0;
    }
}

static ecs_entity_t orePistonIn(ecs_world_t *world, ecs_entity_t e) {
    ecs_iter_t it = ecs_children(world, e);
    while (ecs_children_next(&it)) {
        for (int i = 0; i < it.count; i ++) {
            if (ecs_has(world, it.entities[i], OrePiston)) {
                ecs_iter_fini(&it);
                return it.entities[i];
            }
        }
    }

    it = ecs_children(world, e);
    while (ecs_children_next(&it)) {
        for (int i = 0; i < it.count; i ++) {
            ecs_entity_t found = orePistonIn(world, it.entities[i]);
            if (found) {
                ecs_iter_fini(&it);
                return found;
            }
        }
    }

    return 0;
}

static void OreWorkLights(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    OreWorkLight *light = ecs_field(it, OreWorkLight, 0);
    const FlecsEmissive *emissive = ecs_field(it, FlecsEmissive, 1);
    const FlecsPointLight *point = ecs_field(it, FlecsPointLight, 2);
    const OreClock *clock = ecs_field(it, OreClock, 3);
    const OrePowerState *power = ecs_field(it, OrePowerState, 4);

    bool emissive_self = emissive && ecs_field_is_self(it, 1);
    bool point_self = point && ecs_field_is_self(it, 2);

    float rate = ecs_const_var_get_t(world, "cfg.workLightRate", ecs_f32_t);

    float alpha = it->delta_time * rate;
    if (alpha > 1.0f) {
        alpha = 1.0f;
    }

    for (int i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];

        if (!light[i].owner) {
            light[i].owner = oreWorkOwner(world, e);
        }

        bool starved = light[i].owner && power->blackout &&
            ecs_has(world, light[i].owner, OrePowerConsumer);

        float mul = oreWorkRate(world, power, light[i].owner);
        bool running = mul > 0;
        bool lit = !starved && (light[i].power || running);

        if (lit) {
            light[i].hot = light[i].hold;
        } else if (light[i].hot > 0) {
            light[i].hot -= it->delta_time;
            lit = !starved && light[i].hot > 0;
        }

        if (lit && running && light[i].blink > 0) {
            float phase = clock->time_elapsed * light[i].blink * mul;
            lit = (phase - floorf(phase)) < 0.5f;
        }

        float want = lit ? 1.0f : 0;

        if (light[i].blink > 0) {
            light[i].level = want;
        } else {
            light[i].level += (want - light[i].level) * alpha;
        }

        float level = light[i].level;
        float strength = light[i].off + (light[i].on - light[i].off) * level;

        if (emissive) {
            const FlecsEmissive *cur = emissive_self
                ? &emissive[i]
                : emissive;

            if (cur->strength != strength) {
                FlecsEmissive *dst = ecs_ensure(world, e, FlecsEmissive);
                dst->strength = strength;
                ecs_modified(world, e, FlecsEmissive);
            }
        }

        if (!point) {
            continue;
        }

        float glow = light[i].light * level;

        if (glow > 0 && light[i].pulse > 0 && light[i].owner) {
            if (!light[i].piston) {
                light[i].piston = orePistonIn(world, light[i].owner);
            }

            const OrePiston *piston = light[i].piston
                ? ecs_get(world, light[i].piston, OrePiston)
                : NULL;

            if (piston) {
                float stroke = 0.5f - 0.5f * cosf(piston->phase);
                glow *= 1.0f - light[i].pulse + light[i].pulse * stroke;
            }
        }

        const FlecsPointLight *cur = point_self ? &point[i] : point;

        if (cur->intensity == glow) {
            continue;
        }

        FlecsPointLight *dst = ecs_ensure(world, e, FlecsPointLight);
        dst->intensity = glow;
        ecs_modified(world, e, FlecsPointLight);
    }
}

static void OreStatusLamps(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    OreStatusLamp *lamp = ecs_field(it, OreStatusLamp, 0);
    const FlecsRgba *rgba = ecs_field(it, FlecsRgba, 1);
    const FlecsEmissive *emissive = ecs_field(it, FlecsEmissive, 2);
    const FlecsPointLight *point = ecs_field(it, FlecsPointLight, 3);
    const OreClock *clock = ecs_field(it, OreClock, 4);
    const OrePowerState *power = ecs_field(it, OrePowerState, 5);

    bool rgba_self = ecs_field_is_self(it, 1);
    bool emissive_self = emissive && ecs_field_is_self(it, 2);
    bool point_self = point && ecs_field_is_self(it, 3);

    for (int i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];

        if (!lamp[i].owner) {
            lamp[i].owner = oreOwner(world, ecs_get_target(world, e, EcsChildOf, 0),
                ecs_id(OrePowerConsumer), ecs_id(OrePowerCategory));
            if (!lamp[i].owner) {
                continue;
            }

            lamp[i].consumer =
                ecs_has(world, lamp[i].owner, OrePowerConsumer);
        }

        int32_t level = orePowerLevel(world, lamp[i].owner);

        bool dead = (power->blackout && lamp[i].consumer) || level == 0;
        bool lit = true;

        if (dead && lamp[i].blink > 0) {
            float phase = clock->time_elapsed * lamp[i].blink;
            lit = (phase - floorf(phase)) < 0.5f;
        }

        FlecsRgba color = lamp[i].live;

        if (dead) {
            color = lamp[i].dead;
        } else if (level == 1) {
            color = lamp[i].warn;
        } else if (level == 2) {
            color = lamp[i].ok;
        }

        float strength = lit ? lamp[i].strength : 0;
        float glow = lit ? lamp[i].light : 0;

        if (point) {
            const FlecsPointLight *cur = point_self ? &point[i] : point;

            if (cur->intensity != glow) {
                FlecsPointLight *dst = ecs_ensure(world, e, FlecsPointLight);
                dst->intensity = glow;
                ecs_modified(world, e, FlecsPointLight);
            }
        }

        const FlecsRgba *cur_rgba = rgba_self ? &rgba[i] : rgba;

        if (cur_rgba->r == color.r && cur_rgba->g == color.g &&
            cur_rgba->b == color.b)
        {
            const FlecsEmissive *cur = emissive
                ? (emissive_self ? &emissive[i] : emissive)
                : NULL;

            if (cur && cur->strength == strength) {
                continue;
            }
        }

        FlecsRgba *dst_rgba = ecs_ensure(world, e, FlecsRgba);
        *dst_rgba = color;
        ecs_modified(world, e, FlecsRgba);

        FlecsEmissive *dst_emissive = ecs_ensure(world, e, FlecsEmissive);
        dst_emissive->strength = strength;
        dst_emissive->color = color;
        ecs_modified(world, e, FlecsEmissive);
    }
}

static void OreBuildingAge(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    for (int i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];

        if (ecs_has(world, e, OreAge)) {
            continue;
        }

        ecs_set(world, e, OreAge, {0});
        ecs_set(world, e, OreWear, {-1, -1});
    }
}

static void OreAging(ecs_iter_t *it) {
    OreAge *age = ecs_field(it, OreAge, 0);

    for (int i = 0; i < it->count; i ++) {
        age[i].value += it->delta_time;
    }
}

static float oreWearCurve(float from, float to, float v) {
    if (to <= from) {
        return v >= to ? 1.0f : 0.0f;
    }

    float t = (v - from) / (to - from);

    if (t <= 0) {
        return 0;
    }

    if (t >= 1) {
        return 1.0f;
    }

    return t * t * (3.0f - 2.0f * t);
}

static bool oreWearMoved(float target, float applied, float step) {
    float d = target - applied;

    if (d < 0) {
        d = -d;
    }

    if (d <= 0.0001f) {
        return false;
    }

    return d >= step || target <= 0 || target >= 1.0f;
}

static void oreWearParts(
    ecs_world_t *world,
    ecs_entity_t e,
    float dust,
    float burn)
{
    ecs_iter_t it = ecs_children(world, e);
    while (ecs_children_next(&it)) {
        for (int i = 0; i < it.count; i ++) {
            ecs_entity_t part = it.entities[i];
            ecs_entity_t base = ecs_get_target(world, part, EcsIsA, 0);

            const FlecsSurfaceWear *wear = base
                ? ecs_get(world, base, FlecsSurfaceWear)
                : NULL;

            if (wear) {
                FlecsSurfaceWear w = *wear;
                w.dust = wear->dust * dust;
                w.burn = burn;
                ecs_set_ptr(world, part, FlecsSurfaceWear, &w);
            }

            oreWearParts(world, part, dust, burn);
        }
    }
}

static void OreWearSync(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    const OreAge *age = ecs_field(it, OreAge, 0);
    OreWear *wear = ecs_field(it, OreWear, 1);

    float start = ecs_const_var_get_t(world, "cfg.ageStart", ecs_f32_t);
    float time = ecs_const_var_get_t(world, "cfg.ageTime", ecs_f32_t);
    float from = ecs_const_var_get_t(world, "cfg.burnFrom", ecs_f32_t);
    float to = ecs_const_var_get_t(world, "cfg.burnTo", ecs_f32_t);
    float step = ecs_const_var_get_t(world, "cfg.wearStep", ecs_f32_t);

    for (int i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];

        float t = time > 0 ? age[i].value / time : 1.0f;
        if (t > 1.0f) {
            t = 1.0f;
        }

        float dust = start + (1.0f - start) * t;

        const OreHealth *health = ecs_get(world, e, OreHealth);

        float hurt = 0;
        if (health && health->max > 0) {
            hurt = 1.0f - health->value / health->max;
        }

        float burn = oreWearCurve(from, to, hurt);

        float jitter = (float)((e * 2654435761u >> 11) & 0xff) / 255.0f;
        float bucket = step * (0.6f + 0.8f * jitter);

        if (!oreWearMoved(dust, wear[i].dust, bucket) &&
            !oreWearMoved(burn, wear[i].burn, 0))
        {
            continue;
        }

        wear[i].dust = dust;
        wear[i].burn = burn;

        oreWearParts(world, e, dust, burn);
    }
}

void oreBuildingsImport(ecs_world_t *world) {
    ECS_META_COMPONENT(world, OreBuilding);
    ECS_META_COMPONENT(world, OreAge);
    ECS_META_COMPONENT(world, OreWear);
    ECS_META_COMPONENT(world, OreDrillState);
    ECS_META_COMPONENT(world, OreSpinner);
    ECS_META_COMPONENT(world, OrePiston);
    ECS_META_COMPONENT(world, OreShake);
    ECS_META_COMPONENT(world, OreWorkLight);
    ECS_META_COMPONENT(world, OreStatusLamp);
    ECS_META_COMPONENT(world, OreVent);
    ECS_META_COMPONENT(world, OreWorkEmitter);
    ECS_META_COMPONENT(world, OreFootprint);
    ECS_META_COMPONENT(world, OreBuildLimit);

    ecs_add_pair(world, ecs_id(OreRecipe), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(OreBuildLimit), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(OreFootprint), EcsOnInstantiate, EcsInherit);

    ecs_add_pair(world, ecs_id(OreBuilding), EcsWith, FlecsDynamicTransform);
    ecs_add_pair(world, ecs_id(OreSpinner), EcsWith, ecs_id(FlecsRotation3));
    ecs_add_pair(world, ecs_id(OreSpinner), EcsWith, FlecsDynamicTransform);
    ecs_add_pair(world, ecs_id(OrePiston), EcsWith, FlecsDynamicTransform);

    ecs_entity_t playing = ecs_constant_to_entity(
        world, OreGameState, OreGameStatePlaying);

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "OreBuildingOnRemove" }),
        .query.terms = {
            { .id = ecs_id(OreBuilding) },
            { .id = ecs_id(OreGame) }
        },
        .events = { EcsOnRemove },
        .callback = OreBuildingOnRemove
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "OreBuildingAge" }),
        .query.terms = {
            { .id = ecs_id(OreBuilding) },
            { .id = EcsPrefab, .oper = EcsNot }
        },
        .events = { EcsOnSet },
        .callback = OreBuildingAge
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreAging" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreAge) },
            { .id = ecs_id(OreGame) },
            { .id = ecs_pair_t(OreGameState, playing) }
        },
        .callback = OreAging
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreWearSync" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreAge) },
            { .id = ecs_id(OreWear) },
            { .id = ecs_id(OreGame) },
            { .id = ecs_pair_t(OreGameState, playing) }
        },
        .callback = OreWearSync
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreDrills" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreDrillState) },
            { .id = ecs_id(OreBuilding) },
            { .id = ecs_id(OreGame) },
            { .id = ecs_id(OrePowerState), .inout = EcsIn },
            { .id = ecs_pair_t(OreGameState, playing) }
        },
        .callback = OreDrills
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreSpinners" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreSpinner) },
            { .id = ecs_id(FlecsRotation3) },
            { .id = ecs_id(OrePowerState), .inout = EcsIn }
        },
        .callback = OreSpinners
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OrePistons" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OrePiston) },
            { .id = ecs_id(FlecsPosition3) },
            { .id = ecs_id(OreGame) },
            { .id = ecs_id(OrePowerState), .inout = EcsIn }
        },
        .callback = OrePistons
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreShakes" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreShake) },
            { .id = ecs_id(FlecsPosition3) },
            { .id = ecs_id(OrePowerState), .inout = EcsIn }
        },
        .callback = OreShakes
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreVents" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreVent) },
            { .id = ecs_id(FlecsPosition3) },
            { .id = ecs_id(OreGame) },
            { .id = ecs_id(OrePowerState), .inout = EcsIn }
        },
        .callback = OreVents
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreWorkEmitters" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreWorkEmitter) },
            { .id = ecs_id(FlecsParticleEmitter) },
            { .id = ecs_id(OrePowerState), .inout = EcsIn }
        },
        .callback = OreWorkEmitters
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreWorkLights" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreWorkLight) },
            { .id = ecs_id(FlecsEmissive), .oper = EcsOptional },
            { .id = ecs_id(FlecsPointLight), .oper = EcsOptional },
            { .id = ecs_id(OreClock), .inout = EcsIn },
            { .id = ecs_id(OrePowerState), .inout = EcsIn }
        },
        .callback = OreWorkLights
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreStatusLamps" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreStatusLamp) },
            { .id = ecs_id(FlecsRgba) },
            { .id = ecs_id(FlecsEmissive), .oper = EcsOptional },
            { .id = ecs_id(FlecsPointLight), .oper = EcsOptional },
            { .id = ecs_id(OreClock), .inout = EcsIn },
            { .id = ecs_id(OrePowerState), .inout = EcsIn }
        },
        .callback = OreStatusLamps
    });
}
