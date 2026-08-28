#define ORE_ELSE_ROCKET_IMPL
#include "ore_else.h"

#include <stdio.h>

static const char *ore_clamp_names[] = {
    "clamp_a", "clamp_b", "clamp_c", "clamp_d"
};

static int32_t oreRocketUnits(const OreRocketState *rocket) {
    return rocket->cargo_bays + rocket->life_support;
}

static int32_t oreRocketCapacity(ecs_world_t *world, const OreRocketState *rocket) {
    int32_t per_bay = ecs_const_var_get_t(
        world, "cfg.cargoCapacity", ecs_i32_t);
    return rocket->cargo_bays * per_bay;
}

static float oreRocketBaseY(ecs_world_t *world, ecs_entity_t pad) {
    const OreLaunchPad *launch_pad = pad
        ? ecs_get(world, pad, OreLaunchPad)
        : NULL;
    return launch_pad ? launch_pad->base_y : 0;
}

static void oreTemplateSet(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t tmpl,
    const char **names,
    const double *values,
    int32_t count)
{
    const ecs_type_info_t *ti = ecs_get_type_info(world, tmpl);
    if (!ti || ti->size <= 0 || ti->size > 128) {
        return;
    }

    char buf[128] = {0};

    const void *cur = ecs_get_id(world, e, tmpl);
    if (cur) {
        ecs_os_memcpy(buf, cur, ti->size);
    }

    ecs_meta_cursor_t c = ecs_meta_cursor(world, tmpl, buf);
    if (ecs_meta_push(&c)) {
        return;
    }

    for (int i = 0; i < count; i ++) {
        if (ecs_meta_member(&c, names[i])) {
            return;
        }

        ecs_meta_set_float(&c, values[i]);
    }

    ecs_meta_pop(&c);

    ecs_set_id(world, e, tmpl, (size_t)ti->size, buf);
}

static void oreRocketClamps(
    ecs_world_t *world,
    ecs_entity_t pad,
    float tilt)
{
    if (!pad || !ecs_is_alive(world, pad)) {
        return;
    }

    ecs_entity_t tmpl = ecs_lookup(world, "ore_else.assets.PadClamp");
    if (!tmpl) {
        return;
    }

    const char *names[] = { "tilt" };
    double values[] = { (double)tilt };

    for (int i = 0; i < 4; i ++) {
        ecs_entity_t clamp = oreChild(world, pad, ore_clamp_names[i]);
        if (!clamp) {
            continue;
        }

        oreTemplateSet(world, clamp, tmpl, names, values, 1);
    }
}

static void oreRocketDelete(ecs_world_t *world, OreRocketState *rocket) {
    if (rocket->rocket && ecs_is_alive(world, rocket->rocket)) {
        ecs_delete(world, rocket->rocket);
    }

    rocket->rocket = 0;
}

static void oreRocketBuild(ecs_world_t *world, OreRocketState *rocket) {
    if (!rocket->pad || !ecs_is_alive(world, rocket->pad) ||
        !rocket->kit.root || !rocket->kit.model)
    {
        oreRocketDelete(world, rocket);
        return;
    }

    int32_t parts = rocket->engines + rocket->fuel + rocket->cargo_bays +
        rocket->life_support;
    if (!parts) {
        oreRocketDelete(world, rocket);
        return;
    }

    const FlecsPosition3 *pad_pos = ecs_get(
        world, rocket->pad, FlecsPosition3);
    if (!pad_pos) {
        oreRocketDelete(world, rocket);
        return;
    }

    ecs_entity_t e = rocket->rocket;

    if (!e || !ecs_is_alive(world, e)) {
        e = ecs_new_w_pair(world, EcsChildOf, rocket->kit.root);
        ecs_set(world, e, FlecsPosition3, {
            pad_pos->x, oreRocketBaseY(world, rocket->pad), pad_pos->z});
        ecs_set(world, e, FlecsRotation3, {0, 0, 0});
        ecs_set(world, e, FlecsScale3, {1, 1, 1});
        ecs_add_id(world, e, FlecsDynamicTransform);

        rocket->rocket = e;
    }

    const char *names[] = {
        "engines", "fuel", "cargo", "life_support", "loaded", "valid"
    };

    double values[] = {
        (double)rocket->engines,
        (double)rocket->fuel,
        (double)rocket->cargo_bays,
        (double)rocket->life_support,
        rocket->luminite > 0 ? 1.0 : 0.0,
        rocket->valid ? 1.0 : 0.0
    };

    oreTemplateSet(world, e, rocket->kit.model, names, values, 6);
}

static bool oreRocketRefresh(ecs_world_t *world, OreRocketState *rocket) {
    int32_t per_unit = ecs_const_var_get_t(
        world, "cfg.enginesPerUnit", ecs_i32_t);

    int32_t units = oreRocketUnits(rocket);
    int32_t need_engines = units * per_unit;

    bool valid = units > 0 && rocket->engines == need_engines &&
        rocket->fuel == units;

    bool changed = rocket->valid != valid;

    rocket->valid = valid;

    char buf[96];

    if (rocket->phase != OreLaunchPhaseIdle) {
        snprintf(buf, sizeof(buf), "LAUNCHING");
    } else if (rocket->valid) {
        snprintf(buf, sizeof(buf), "Rocket ready to launch (P)");
    } else if (!units) {
        snprintf(buf, sizeof(buf), "Install a cargo bay (U) or life support (I)");
    } else if (rocket->engines != need_engines) {
        snprintf(buf, sizeof(buf), "Needs %d engines, %d installed (T)",
            need_engines, rocket->engines);
    } else {
        snprintf(buf, sizeof(buf), "Needs %d fuel tanks, %d installed (Y)",
            units, rocket->fuel);
    }

    return oreTextSet(&rocket->status_text, buf) || changed;
}

static void oreRocketScrap(ecs_world_t *world, OreRocketState *rocket) {
    oreRocketDelete(world, rocket);
    oreRocketClamps(world, rocket->pad, 0);

    rocket->engines = 0;
    rocket->fuel = 0;
    rocket->cargo_bays = 0;
    rocket->life_support = 0;
    rocket->luminite = 0;
    rocket->phase = OreLaunchPhaseIdle;
    rocket->timer = 0;
    rocket->speed = 0;
    rocket->fx_timer = 0;

    oreRocketRefresh(world, rocket);
}

void oreRocketReset(ecs_world_t *world, OreRocketState *rocket) {
    rocket->pad = 0;
    rocket->boarded = false;

    oreRocketScrap(world, rocket);

    if (rocket->kit.root) {
        ecs_delete_with(world, ecs_pair(EcsChildOf, rocket->kit.root));
    }
}

static bool oreRocketUninstall(ecs_world_t *world, ecs_entity_t item) {
    OreRocketState *rocket = ecs_singleton_ensure(world, OreRocketState);

    if (!item || !rocket->pad || rocket->phase != OreLaunchPhaseIdle) {
        return false;
    }

    int32_t per_unit = ecs_const_var_get_t(
        world, "cfg.enginesPerUnit", ecs_i32_t);
    int32_t per_bay = ecs_const_var_get_t(
        world, "cfg.cargoCapacity", ecs_i32_t);

    int32_t engines = rocket->engines;
    int32_t fuel = rocket->fuel;
    int32_t bays = rocket->cargo_bays;
    int32_t life = rocket->life_support;

    if (item == rocket->kit.engine_item) {
        if (!engines) {
            return false;
        }
        engines --;
    } else if (item == rocket->kit.fuel_item) {
        if (!fuel) {
            return false;
        }
        fuel --;
    } else if (item == rocket->kit.cargo_item) {
        if (!bays) {
            return false;
        }
        bays --;
    } else if (item == rocket->kit.life_item) {
        if (!life) {
            return false;
        }
        life --;
    } else {
        return false;
    }

    int32_t units = bays + life;
    int32_t max_engines = units * per_unit;

    int32_t spare_engines = engines > max_engines ? engines - max_engines : 0;
    int32_t spare_fuel = fuel > units ? fuel - units : 0;

    engines -= spare_engines;
    fuel -= spare_fuel;

    int32_t capacity = bays * per_bay;
    int32_t spare_luminite = rocket->luminite > capacity
        ? rocket->luminite - capacity
        : 0;

    ecs_entity_t items[4];
    int32_t amounts[4];
    int32_t count = 0;

    items[count] = item;
    amounts[count ++] = 1;

    if (spare_engines) {
        items[count] = rocket->kit.engine_item;
        amounts[count ++] = spare_engines;
    }

    if (spare_fuel) {
        items[count] = rocket->kit.fuel_item;
        amounts[count ++] = spare_fuel;
    }

    if (spare_luminite) {
        items[count] = rocket->kit.luminite;
        amounts[count ++] = spare_luminite;
    }

    if (!oreInventoryFits(world, items, NULL, count)) {
        oreToast(world, "No room in the inventory for the parts", true);
        return false;
    }

    for (int32_t i = 0; i < count; i ++) {
        oreInventoryAdd(world, items[i], amounts[i]);
    }

    rocket->engines = engines;
    rocket->fuel = fuel;
    rocket->cargo_bays = bays;
    rocket->life_support = life;
    rocket->luminite -= spare_luminite;

    if (spare_engines || spare_fuel || spare_luminite) {
        char parts[80] = "";
        int32_t len = 0;

        if (spare_engines) {
            len += snprintf(parts + len, sizeof(parts) - len, "%d %s",
                spare_engines, spare_engines == 1 ? "engine" : "engines");
        }

        if (spare_fuel) {
            len += snprintf(parts + len, sizeof(parts) - len, "%s%d %s",
                len ? ", " : "", spare_fuel,
                spare_fuel == 1 ? "fuel tank" : "fuel tanks");
        }

        if (spare_luminite) {
            snprintf(parts + len, sizeof(parts) - len, "%s%d Luminite",
                len ? ", " : "", spare_luminite);
        }

        char buf[128];
        snprintf(buf, sizeof(buf), "Payload removed - returned %s", parts);

        oreToast(world, buf, false);
    }

    oreRocketRefresh(world, rocket);
    oreRocketBuild(world, rocket);

    ecs_singleton_modified(world, OreRocketState);
    return true;
}

static bool oreRocketInstall(ecs_world_t *world, ecs_entity_t item) {
    OreRocketState *rocket = ecs_singleton_ensure(world, OreRocketState);

    if (!item || !rocket->pad || rocket->phase != OreLaunchPhaseIdle) {
        return false;
    }

    if (oreInventoryGet(world, item) < 1) {
        return false;
    }

    int32_t max_bays = ecs_const_var_get_t(
        world, "cfg.maxCargoBays", ecs_i32_t);
    int32_t max_life = ecs_const_var_get_t(
        world, "cfg.maxLifeSupport", ecs_i32_t);
    int32_t per_unit = ecs_const_var_get_t(
        world, "cfg.enginesPerUnit", ecs_i32_t);

    int32_t units = oreRocketUnits(rocket);

    if (item == rocket->kit.engine_item) {
        if (rocket->engines >= units * per_unit) {
            oreToast(world,
                "Engines full for this payload - add a cargo bay (U) first",
                true);
            return false;
        }
        rocket->engines ++;
    } else if (item == rocket->kit.fuel_item) {
        if (rocket->fuel >= units) {
            oreToast(world,
                "Fuel tanks full for this payload - add a cargo bay (U) first",
                true);
            return false;
        }
        rocket->fuel ++;
    } else if (item == rocket->kit.cargo_item) {
        if (rocket->cargo_bays >= max_bays) {
            return false;
        }
        rocket->cargo_bays ++;
    } else if (item == rocket->kit.life_item) {
        if (rocket->life_support >= max_life) {
            return false;
        }
        rocket->life_support ++;
    } else {
        return false;
    }

    oreInventoryAdd(world, item, -1);

    oreRocketRefresh(world, rocket);
    oreRocketBuild(world, rocket);

    ecs_singleton_modified(world, OreRocketState);
    return true;
}

static int32_t oreRocketPending(
    ecs_world_t *world,
    const OreRocketState *rocket)
{
    int32_t room = oreRocketCapacity(world, rocket) - rocket->luminite;
    if (room <= 0) {
        return 0;
    }

    int32_t have = oreInventoryGet(world, rocket->kit.luminite);
    return have < room ? have : room;
}

static int32_t oreRocketFill(ecs_world_t *world, OreRocketState *rocket) {
    int32_t amount = oreRocketPending(world, rocket);
    if (amount <= 0) {
        return 0;
    }

    oreInventoryAdd(world, rocket->kit.luminite, -amount);
    rocket->luminite += amount;

    return amount;
}

static bool oreRocketLoad(ecs_world_t *world) {
    OreRocketState *rocket = ecs_singleton_ensure(world, OreRocketState);

    if (!rocket->pad || rocket->phase != OreLaunchPhaseIdle) {
        return false;
    }

    if (!oreRocketFill(world, rocket)) {
        return false;
    }

    oreRocketRefresh(world, rocket);
    oreRocketBuild(world, rocket);

    ecs_singleton_modified(world, OreRocketState);
    return true;
}

static bool oreRocketLaunch(ecs_world_t *world) {
    OreRocketState *rocket = ecs_singleton_ensure(world, OreRocketState);

    if (!rocket->valid || rocket->phase != OreLaunchPhaseIdle) {
        return false;
    }

    float tilt = ecs_const_var_get_t(world, "cfg.clampTilt", ecs_f32_t);

    oreRocketFill(world, rocket);

    rocket->phase = OreLaunchPhaseIgnition;
    rocket->timer = ecs_const_var_get_t(world, "cfg.ignitionTime", ecs_f32_t);
    rocket->speed = 0;
    rocket->fx_timer = 0;

    oreRocketClamps(world, rocket->pad, tilt);

    if (rocket->life_support > 0) {
        const OreGame *game = ecs_singleton_get(world, OreGame);
        ecs_entity_t player = game ? game->player : 0;

        const FlecsPosition3 *pad_pos = ecs_get(
            world, rocket->pad, FlecsPosition3);

        if (pad_pos && player) {
            ecs_set(world, player, FlecsPosition3, {
                pad_pos->x, 0, pad_pos->z});
        }

        if (player) {
            ecs_set(world, player, FlecsScale3, {0.001f, 0.001f, 0.001f});
        }

        rocket->boarded = true;
    }

    oreRocketRefresh(world, rocket);
    oreRocketBuild(world, rocket);

    ecs_singleton_modified(world, OreRocketState);
    return true;
}

static ecs_entity_t oreRocketFindPad(
    ecs_world_t *world,
    const OreRocketState *rocket)
{
    ecs_entity_t pad = 0;

    ecs_iter_t it = ecs_query_iter(world, rocket->pad_query);
    while (!pad && ecs_query_next(&it)) {
        if (it.count) {
            pad = it.entities[0];
        }
    }

    if (pad) {
        ecs_iter_fini(&it);
    }

    return pad;
}

static void oreRocketFx(
    ecs_iter_t *it,
    const OreGame *game,
    OreRocketState *rocket,
    float y)
{
    ecs_world_t *world = it->world;

    float interval = ecs_const_var_get_t(
        world, "cfg.launchFxInterval", ecs_f32_t);

    rocket->fx_timer -= it->delta_time;

    if (rocket->fx_timer > 0) {
        return;
    }

    rocket->fx_timer = interval;

    const FlecsPosition3 *pos = ecs_get(world, rocket->rocket, FlecsPosition3);
    if (!pos) {
        return;
    }

    oreBurst(world, game->glow_pool, rocket->kit.flame, pos->x, y, pos->z);
    oreBurst(world, game->fx_pool, rocket->kit.smoke, pos->x, y, pos->z);
}

static void oreRocketArrive(
    ecs_world_t *world,
    OreRocketState *rocket)
{
    bool crewed = rocket->life_support > 0;
    int32_t shipped = rocket->luminite;

    OreLuminiteProgress *luminite =
        ecs_singleton_ensure(world, OreLuminiteProgress);
    luminite->luminite_shipped += shipped;
    ecs_singleton_modified(world, OreLuminiteProgress);

    oreRocketScrap(world, rocket);

    if (crewed) {
        oreSetState(world, OreGameStateWon);
        return;
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "Shipment away!  +%d Luminite", shipped);
    oreToast(world, buf, false);
}

static void oreRocketFly(
    ecs_iter_t *it,
    OreGame *game,
    OreRocketState *rocket)
{
    ecs_world_t *world = it->world;

    if (!rocket->rocket || !ecs_is_alive(world, rocket->rocket)) {
        rocket->phase = OreLaunchPhaseIdle;
        return;
    }

    float base_y = oreRocketBaseY(world, rocket->pad);

    if (rocket->phase == OreLaunchPhaseIgnition) {
        float shake = ecs_const_var_get_t(world, "cfg.launchShake", ecs_f32_t);

        rocket->timer -= it->delta_time;

        const OreClock *clock = ecs_singleton_get(world, OreClock);

        ecs_set(world, rocket->rocket, FlecsRotation3, {
            sinf(clock->time_elapsed * 47.0f) * shake, 0,
            cosf(clock->time_elapsed * 39.0f) * shake});

        oreRocketFx(it, game, rocket, base_y);

        if (rocket->timer <= 0) {
            rocket->phase = OreLaunchPhaseAscent;
            rocket->speed = 0;

            ecs_set(world, rocket->rocket, FlecsRotation3, {0, 0, 0});
        }

        return;
    }

    float accel = ecs_const_var_get_t(world, "cfg.launchAccel", ecs_f32_t);
    float altitude = ecs_const_var_get_t(
        world, "cfg.launchAltitude", ecs_f32_t);

    rocket->speed += accel * it->delta_time;

    const FlecsPosition3 *pos = ecs_get(world, rocket->rocket, FlecsPosition3);
    if (!pos) {
        return;
    }

    float y = pos->y + rocket->speed * it->delta_time;

    ecs_set(world, rocket->rocket, FlecsPosition3, {pos->x, y, pos->z});

    oreRocketFx(it, game, rocket, y);

    if (y - base_y >= altitude) {
        oreRocketArrive(world, rocket);
    }
}

static void OreRocket(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    OreGame *game = ecs_field(it, OreGame, 0);
    OreRocketState *rocket = ecs_field(it, OreRocketState, 1);

    bool changed = false;

    ecs_entity_t pad = oreRocketFindPad(world, rocket);

    if (pad != rocket->pad) {
        rocket->pad = pad;
        changed = true;

        if (!pad && rocket->phase == OreLaunchPhaseIdle) {
            oreRocketScrap(world, rocket);
        }
    }

    if (oreRocketRefresh(world, rocket)) {
        changed = true;
    }

    if (rocket->phase != OreLaunchPhaseIdle) {
        oreRocketFly(it, game, rocket);
        changed = true;
    }

    if (changed) {
        ecs_singleton_modified(world, OreRocketState);
    }
}

void oreRocketClick(ecs_world_t *world, ecs_entity_t widget) {
    if (!orePlaying(world)) {
        return;
    }

    const OreRocketPacket *packet = ecs_get(world, widget, OreRocketPacket);
    if (!packet) {
        return;
    }

    if (packet->launch) {
        oreRocketLaunch(world);
    } else if (packet->load) {
        oreRocketLoad(world);
    } else if (packet->remove) {
        oreRocketUninstall(world, packet->item);
    } else {
        oreRocketInstall(world, packet->item);
    }
}

static bool oreRocketEnabled(
    ecs_world_t *world,
    const OreRocketState *rocket,
    const OreRocketPacket *packet)
{
    if (!rocket->pad || rocket->phase != OreLaunchPhaseIdle) {
        return false;
    }

    if (packet->launch) {
        return rocket->valid;
    }

    if (packet->load) {
        return oreRocketPending(world, rocket) > 0;
    }

    if (packet->remove) {
        if (packet->item == rocket->kit.engine_item) {
            return rocket->engines > 0;
        }
        if (packet->item == rocket->kit.fuel_item) {
            return rocket->fuel > 0;
        }
        if (packet->item == rocket->kit.cargo_item) {
            return rocket->cargo_bays > 0;
        }
        if (packet->item == rocket->kit.life_item) {
            return rocket->life_support > 0;
        }
        return false;
    }

    if (oreInventoryGet(world, packet->item) < 1) {
        return false;
    }

    if (packet->item == rocket->kit.cargo_item) {
        return rocket->cargo_bays < ecs_const_var_get_t(
            world, "cfg.maxCargoBays", ecs_i32_t);
    }

    if (packet->item == rocket->kit.life_item) {
        return rocket->life_support < ecs_const_var_get_t(
            world, "cfg.maxLifeSupport", ecs_i32_t);
    }

    int32_t units = oreRocketUnits(rocket);

    if (packet->item == rocket->kit.engine_item) {
        return rocket->engines < units * ecs_const_var_get_t(
            world, "cfg.enginesPerUnit", ecs_i32_t);
    }

    if (packet->item == rocket->kit.fuel_item) {
        return rocket->fuel < units;
    }

    return true;
}

static void oreRocketNote(
    ecs_world_t *world,
    const OreRocketState *rocket,
    char *buf,
    size_t size)
{
    int32_t per_unit = ecs_const_var_get_t(
        world, "cfg.enginesPerUnit", ecs_i32_t);
    int32_t units = rocket->cargo_bays + rocket->life_support;
    int32_t need_engines = units * per_unit;

    if (rocket->phase != OreLaunchPhaseIdle) {
        snprintf(buf, size, "LIFTOFF");
    } else if (!units) {
        snprintf(buf, size, "FIT A CARGO BAY OR LIFE SUPPORT");
    } else if (rocket->engines != need_engines) {
        snprintf(buf, size, "NEEDS %d MORE ENGINES",
            need_engines - rocket->engines);
    } else if (rocket->fuel != units) {
        snprintf(buf, size, "NEEDS %d MORE FUEL TANKS",
            units - rocket->fuel);
    } else {
        int32_t aboard = rocket->luminite + oreRocketPending(world, rocket);

        if (rocket->life_support > 0) {
            snprintf(buf, size, "CREW + %d LUMINITE ABOARD", aboard);
        } else {
            snprintf(buf, size, "%d LUMINITE ABOARD", aboard);
        }
    }
}

static void OreRocketUiBind(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    OreRocketState *rocket = ecs_field(it, OreRocketState, 0);

    char note[64];
    oreRocketNote(world, rocket, note, sizeof(note));

    if (oreTextSet(&rocket->note, note)) {
        ecs_singleton_modified(world, OreRocketState);
    }

    ecs_iter_t wit = ecs_each_id(world, ecs_id(OreRocketPacket));
    while (ecs_each_next(&wit)) {
        const OreRocketPacket *packet = ecs_field(&wit, OreRocketPacket, 0);

        for (int i = 0; i < wit.count; i ++) {
            oreAffordSet(world, wit.entities[i],
                oreRocketEnabled(world, rocket, &packet[i]));
        }
    }
}

void oreRocketImport(ecs_world_t *world) {
    ECS_META_COMPONENT(world, OreLaunchPhase);
    ECS_META_COMPONENT(world, OreRocketKit);
    ECS_META_COMPONENT(world, OreRocketState);
    ECS_META_COMPONENT(world, OreLaunchPad);
    ECS_META_COMPONENT(world, OreRocketPacket);

    ecs_add_id(world, ecs_id(OreRocketState), EcsSingleton);

    ecs_add_pair(world, ecs_id(OreRocketPacket), EcsWith,
        ecs_id(FlecsUiWidgetState));

    OreRocketState *rocket = ecs_singleton_ensure(world, OreRocketState);

    rocket->pad_query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "OrePadQuery" }),
        .terms = {
            { .id = ecs_id(OreLaunchPad), .inout = EcsInOutNone },
            { .id = ecs_id(OreBuilding), .inout = EcsInOutNone },
            { .id = ecs_id(FlecsPosition3), .inout = EcsIn }
        }
    });

    ecs_singleton_modified(world, OreRocketState);

    ecs_entity_t playing = ecs_constant_to_entity(
        world, OreGameState, OreGameStatePlaying);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreRocket" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreGame) },
            { .id = ecs_id(OreRocketState) },
            { .id = ecs_pair_t(OreGameState, playing) }
        },
        .callback = OreRocket
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreRocketUiBind" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreRocketState) }
        },
        .callback = OreRocketUiBind
    });
}
