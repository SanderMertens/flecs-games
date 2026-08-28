#include "ore_else.h"

#include <math.h>
#include <stdlib.h>

#define ORE_STRESS_CENTER (24)
#define ORE_STRESS_EDGE (10)
#define ORE_STRESS_FAR (ORE_ROWS - 1 - ORE_STRESS_EDGE)
#define ORE_STRESS_POWER_MARGIN (1.15f)

typedef struct {
    ecs_world_t *world;
    OreGame *game;
    OreMap map;
    ecs_entity_t wall;
    ecs_entity_t drill;
    ecs_entity_t solar;
    ecs_entity_t gun;
    ecs_entity_t laser;
    ecs_entity_t missile;
    ecs_entity_t factory;
    ecs_entity_t drone_pad;
    ecs_entity_t rocket_pad;
    int32_t placed;
    int32_t turrets;
    float demand;
    float prod;
} ore_stress_t;

typedef struct {
    const char *name;
    int32_t amount;
} ore_stress_stock_t;

static float ore_stress_minutes = 0;

static const ore_stress_stock_t ore_stress_stock[] = {
    { "Iron", 500 },
    { "Copper", 500 },
    { "Stone", 500 },
    { "Luminite", 200 },
    { "IronPlate", 200 },
    { "CopperPlate", 200 },
    { "StoneBrick", 200 },
    { "Wall", 40 },
    { "Drill", 6 },
    { "SolarPanel", 10 },
    { "GunTurret", 6 },
    { "LaserTurret", 4 },
    { "MissileTurret", 4 }
};

void oreStressTestSet(float minutes) {
    ore_stress_minutes = minutes;
}

bool oreStressTestEnabled(void) {
    return ore_stress_minutes > 0;
}

static ecs_entity_t oreStressLookup(ecs_world_t *world, const char *name) {
    static const char *scopes[] = {
        "ore_else.assets", "ore_else.resources"
    };

    for (int32_t i = 0; i < 2; i ++) {
        char path[128];
        snprintf(path, sizeof(path), "%s.%s", scopes[i], name);
        ecs_entity_t e = ecs_lookup(world, path);
        if (e) {
            return e;
        }
    }

    ecs_err("stresstest: unknown entity '%s'", name);
    return 0;
}

static bool oreStressPlace(
    ore_stress_t *s,
    ecs_entity_t prefab,
    int32_t row,
    int32_t col)
{
    if (!oreCellFree(s->world, s->game, prefab, row, col)) {
        return false;
    }

    orePlaceBuilding(s->world, s->game, &s->map, prefab, row, col);

    const OrePowerConsumer *c = ecs_get(s->world, prefab, OrePowerConsumer);
    if (c) {
        s->demand += c->watts;
    }

    const OrePowerProducer *p = ecs_get(s->world, prefab, OrePowerProducer);
    if (p) {
        s->prod += p->watts;
    }

    if (ecs_has(s->world, prefab, OreTurret)) {
        s->turrets ++;
    }

    s->placed ++;
    return true;
}

static bool oreStressPlaceNear(
    ore_stress_t *s,
    ecs_entity_t prefab,
    int32_t row,
    int32_t col,
    int32_t radius)
{
    for (int32_t d = 0; d <= radius; d ++) {
        for (int32_t r = row - d; r <= row + d; r ++) {
            for (int32_t c = col - d; c <= col + d; c ++) {
                if (abs(r - row) != d && abs(c - col) != d) {
                    continue;
                }

                if (oreStressPlace(s, prefab, r, c)) {
                    return true;
                }
            }
        }
    }

    return false;
}

static void oreStressDrills(ore_stress_t *s) {
    for (int32_t row = 0; row < ORE_ROWS; row ++) {
        for (int32_t col = 0; col < ORE_COLS; col ++) {
            if (*oreDepositCell(s->game, row, col)) {
                oreStressPlace(s, s->drill, row, col);
            }
        }
    }
}

static void oreStressRing(
    ore_stress_t *s,
    int32_t inset,
    int32_t step,
    const ecs_entity_t *prefabs,
    int32_t prefab_count)
{
    int32_t lo = ORE_STRESS_EDGE + inset;
    int32_t hi = ORE_STRESS_FAR - inset;
    int32_t n = 0;

    for (int32_t i = lo; i <= hi; i += step, n ++) {
        oreStressPlace(s, prefabs[n % prefab_count], lo, i);
        oreStressPlace(s, prefabs[(n + 1) % prefab_count], hi, hi - (i - lo));
        oreStressPlace(s, prefabs[(n + 2) % prefab_count], hi - (i - lo), lo);
        oreStressPlace(s, prefabs[(n + 3) % prefab_count], i, hi);
    }
}

static void oreStressSolar(ore_stress_t *s) {
    for (int32_t inset = 3; inset < ORE_STRESS_CENTER - ORE_STRESS_EDGE;
        inset ++)
    {
        int32_t lo = ORE_STRESS_EDGE + inset;
        int32_t hi = ORE_STRESS_FAR - inset;

        for (int32_t i = lo; i <= hi; i ++) {
            if (s->prod >= s->demand * ORE_STRESS_POWER_MARGIN) {
                return;
            }

            oreStressPlace(s, s->solar, lo, i);
            oreStressPlace(s, s->solar, hi, i);
            oreStressPlace(s, s->solar, i, lo);
            oreStressPlace(s, s->solar, i, hi);
        }
    }
}

static void oreStressBase(ore_stress_t *s) {
    ecs_entity_t turrets[] = { s->gun, s->laser, s->gun, s->missile };

    oreStressDrills(s);
    oreStressRing(s, 0, 1, &s->wall, 1);
    oreStressRing(s, 1, 1, &s->wall, 1);
    oreStressRing(s, 2, 1, turrets, 4);

    oreStressPlaceNear(s, s->rocket_pad,
        ORE_STRESS_CENTER + 5, ORE_STRESS_CENTER - 1, 8);
    oreStressPlaceNear(s, s->factory,
        ORE_STRESS_CENTER - 4, ORE_STRESS_CENTER - 4, 8);
    oreStressPlaceNear(s, s->factory,
        ORE_STRESS_CENTER - 4, ORE_STRESS_CENTER + 3, 8);
    oreStressPlaceNear(s, s->drone_pad,
        ORE_STRESS_CENTER + 3, ORE_STRESS_CENTER + 4, 8);
    oreStressPlaceNear(s, s->drone_pad,
        ORE_STRESS_CENTER - 1, ORE_STRESS_CENTER - 6, 8);

    oreStressSolar(s);
}

static void oreStressPlayer(ore_stress_t *s) {
    ecs_entity_t player = s->game->player;
    if (!player || !ecs_is_alive(s->world, player)) {
        return;
    }

    for (int32_t d = 0; d < ORE_STRESS_CENTER; d ++) {
        for (int32_t r = ORE_STRESS_CENTER - d; r <= ORE_STRESS_CENTER + d;
            r ++)
        {
            for (int32_t c = ORE_STRESS_CENTER - d; c <= ORE_STRESS_CENTER + d;
                c ++)
            {
                if (!oreOnGrid(r, c) || *oreBuildingCell(s->game, r, c) ||
                    *oreDepositCell(s->game, r, c))
                {
                    continue;
                }

                ecs_set(s->world, player, FlecsPosition3, {
                    oreTileX(&s->map, c), 0, oreTileZ(&s->map, r)
                });
                return;
            }
        }
    }
}

static void oreStressStock(ecs_world_t *world) {
    int32_t count = (int32_t)(sizeof(ore_stress_stock) /
        sizeof(ore_stress_stock[0]));

    for (int32_t i = 0; i < count; i ++) {
        ecs_entity_t item = oreStressLookup(world, ore_stress_stock[i].name);
        if (item) {
            oreInventoryAdd(world, item, ore_stress_stock[i].amount);
        }
    }
}

static void oreStressClock(ecs_world_t *world, float seconds) {
    float first = ecs_const_var_get_t(world, "cfg.firstWaveAt", ecs_f32_t);
    float gap = ecs_const_var_get_t(world, "cfg.waveGap", ecs_f32_t);

    int32_t wave = 0;
    if (seconds >= first && gap > 0) {
        wave = (int32_t)floorf((seconds - first) / gap) + 1;
    }

    OreWaveState *waves = ecs_singleton_ensure(world, OreWaveState);
    waves->wave = wave > 0 ? wave - 1 : 0;
    waves->timer = 10;
    waves->wave_span = 10;
    ecs_singleton_modified(world, OreWaveState);

    OreClock *clock = ecs_singleton_ensure(world, OreClock);
    clock->time_elapsed = seconds;
    ecs_singleton_modified(world, OreClock);
}

void oreStressTestApply(ecs_world_t *world) {
    if (!oreStressTestEnabled()) {
        return;
    }

    OreGame *game = ecs_singleton_ensure(world, OreGame);

    ore_stress_t s = {
        .world = world,
        .game = game,
        .map = ecs_const_var_get_t(world, "cfg.map", OreMap),
        .wall = oreStressLookup(world, "Wall"),
        .drill = oreStressLookup(world, "Drill"),
        .solar = oreStressLookup(world, "SolarPanel"),
        .gun = oreStressLookup(world, "GunTurret"),
        .laser = oreStressLookup(world, "LaserTurret"),
        .missile = oreStressLookup(world, "MissileTurret"),
        .factory = oreStressLookup(world, "Factory"),
        .drone_pad = oreStressLookup(world, "DronePad"),
        .rocket_pad = oreStressLookup(world, "RocketPad")
    };

    if (!s.wall || !s.drill || !s.solar || !s.gun || !s.laser ||
        !s.missile || !s.factory || !s.drone_pad || !s.rocket_pad)
    {
        return;
    }

    oreStressBase(&s);
    oreStressPlayer(&s);
    oreStressStock(world);
    oreStressClock(world, ore_stress_minutes * 60);

    ecs_singleton_modified(world, OreGame);

    ecs_trace("stresstest: %d buildings, %d turrets, power %.0f / %.0f W, "
        "next wave %d", s.placed, s.turrets, s.prod, s.demand,
        ecs_singleton_get(world, OreWaveState)->wave + 1);
}
