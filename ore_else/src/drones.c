#define ORE_ELSE_DRONES_IMPL
#include "ore_else.h"
#include <float.h>

#include <math.h>

static ecs_query_t *ore_drone_query;
static ecs_query_t *ore_repair_query;
static ecs_entity_t ore_drone_root;
static ecs_entity_t ore_drone_iron;

static ecs_entity_t oreDroneIron(ecs_world_t *world) {
    if (!ore_drone_iron || !ecs_is_alive(world, ore_drone_iron)) {
        ore_drone_iron = ecs_lookup(world, "ore_else.resources.Iron");
    }

    return ore_drone_iron;
}

static void oreDroneBayPos(
    const FlecsPosition3 *pad,
    int32_t bay,
    int32_t count,
    float radius,
    float height,
    float *out)
{
    if (count < 1) {
        count = 1;
    }

    float a = ((float)bay + 0.5f) * 6.2831853f / (float)count;

    out[0] = pad->x + sinf(a) * radius;
    out[1] = height;
    out[2] = pad->z + cosf(a) * radius;
}

static bool oreDroneClaimed(
    ecs_world_t *world,
    ecs_entity_t pad,
    ecs_entity_t self,
    ecs_entity_t target)
{
    ecs_iter_t it = ecs_query_iter(world, ore_drone_query);

    while (ecs_query_next(&it)) {
        const OreDrone *drone = ecs_field(&it, OreDrone, 0);

        for (int i = 0; i < it.count; i ++) {
            if (it.entities[i] == self || drone[i].pad != pad) {
                continue;
            }

            if (drone[i].target == target) {
                ecs_iter_fini(&it);
                return true;
            }
        }
    }

    return false;
}

static bool oreDroneHasAny(ecs_world_t *world, ecs_entity_t pad) {
    ecs_iter_t it = ecs_query_iter(world, ore_drone_query);

    while (ecs_query_next(&it)) {
        const OreDrone *drone = ecs_field(&it, OreDrone, 0);

        for (int i = 0; i < it.count; i ++) {
            if (drone[i].pad == pad) {
                ecs_iter_fini(&it);
                return true;
            }
        }
    }

    return false;
}

static ecs_entity_t oreDroneAcquire(
    ecs_world_t *world,
    ecs_entity_t self,
    ecs_entity_t pad,
    const FlecsPosition3 *origin,
    float range)
{
    float best = range > 0 ? range * range : FLT_MAX;
    ecs_entity_t result = 0;

    ecs_iter_t it = ecs_query_iter(world, ore_repair_query);

    while (ecs_query_next(&it)) {
        const OreHealth *health = ecs_field(&it, OreHealth, 1);
        const FlecsPosition3 *pos = ecs_field(&it, FlecsPosition3, 2);

        for (int i = 0; i < it.count; i ++) {
            if (health[i].value <= 0 || health[i].value >= health[i].max) {
                continue;
            }

            float dx = pos[i].x - origin->x;
            float dz = pos[i].z - origin->z;
            float d2 = dx * dx + dz * dz;

            if (d2 >= best) {
                continue;
            }

            if (oreDroneClaimed(world, pad, self, it.entities[i])) {
                continue;
            }

            best = d2;
            result = it.entities[i];
        }
    }

    return result;
}

static bool oreDroneDamaged(ecs_world_t *world, ecs_entity_t target) {
    if (!target || !ecs_is_alive(world, target)) {
        return false;
    }

    const OreHealth *health = ecs_get(world, target, OreHealth);
    if (!health || health->value <= 0 || health->value >= health->max) {
        return false;
    }

    return ecs_get(world, target, FlecsPosition3) != NULL;
}

static bool oreDroneTake(
    ecs_world_t *world,
    ecs_entity_t e,
    OreDrone *drone,
    const FlecsPosition3 *pad,
    float range)
{
    ecs_entity_t next = oreDroneAcquire(world, e, drone->pad, pad, range);

    if (!next) {
        return false;
    }

    drone->target = next;

    return true;
}

static void oreDroneCache(ecs_world_t *world, ecs_entity_t e, OreDrone *drone) {
    static const char *names[ORE_DRONE_ROTORS] = {
        "rotor_a", "rotor_b", "rotor_c", "rotor_d"
    };

    if (!drone->fx) {
        drone->fx = oreChild(world, e, "weld");
    }

    for (int r = 0; r < ORE_DRONE_ROTORS; r ++) {
        if (!drone->rotors[r]) {
            drone->rotors[r] = oreChild(world, e, names[r]);
        }
    }
}

static void oreDroneSpin(
    ecs_world_t *world,
    OreDrone *drone,
    const OreFlight *flight,
    float dt)
{
    float want = drone->state == OreDroneStateDocked ? 0 : flight->rotor;
    float step = flight->spin_up * flight->rotor * dt;

    if (drone->spin < want) {
        drone->spin += step;
        if (drone->spin > want) {
            drone->spin = want;
        }
    } else if (drone->spin > want) {
        drone->spin -= step;
        if (drone->spin < want) {
            drone->spin = want;
        }
    }

    for (int r = 0; r < ORE_DRONE_ROTORS; r ++) {
        ecs_entity_t rotor = drone->rotors[r];

        if (!rotor || !ecs_is_alive(world, rotor)) {
            continue;
        }

        OreSpinner *spinner = ecs_get_mut(world, rotor, OreSpinner);
        if (!spinner) {
            continue;
        }

        float speed = (r & 1) ? -drone->spin : drone->spin;

        if (spinner->speed != speed) {
            spinner->speed = speed;
        }
    }
}

static void oreDroneMove(
    FlecsPosition3 *pos,
    FlecsRotation3 *rot,
    const float *goal,
    float speed,
    const OreFlight *flight,
    float dt)
{
    float dx = goal[0] - pos->x;
    float dy = goal[1] - pos->y;
    float dz = goal[2] - pos->z;

    float dist = sqrtf(dx * dx + dy * dy + dz * dz);

    if (dist > 0.0001f) {
        float v = speed;

        if (flight->ease > 0 && dist < flight->ease) {
            v = speed * (0.2f + 0.8f * dist / flight->ease);
        }

        float step = v * dt;

        if (step > dist) {
            step = dist;
        }

        pos->x += dx / dist * step;
        pos->y += dy / dist * step;
        pos->z += dz / dist * step;
    }

    float h = sqrtf(dx * dx + dz * dz);

    if (h > 0.08f) {
        float want = atan2f(dx, dz);
        float diff = want - rot->y;

        while (diff > 3.1415927f) {
            diff -= 6.2831853f;
        }

        while (diff < -3.1415927f) {
            diff += 6.2831853f;
        }

        float max = flight->turn * dt;

        if (diff > max) {
            diff = max;
        }

        if (diff < -max) {
            diff = -max;
        }

        rot->y += diff;
    }
}

static void OreDronePadOnSet(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    for (int i = 0; i < it->count; i ++) {
        ecs_entity_t pad = it->entities[i];

        const OreDroneBay *bay = ecs_get(world, pad, OreDroneBay);
        if (!bay || !bay->drone || bay->count <= 0) {
            continue;
        }

        const FlecsPosition3 *pos = ecs_get(world, pad, FlecsPosition3);
        if (!pos) {
            continue;
        }

        if (oreDroneHasAny(world, pad)) {
            continue;
        }

        for (int b = 0; b < bay->count; b ++) {
            float p[3];
            oreDroneBayPos(pos, b, bay->count, bay->radius, bay->dock, p);

            ecs_entity_t drone = ecs_new_w_pair(world, EcsIsA, bay->drone);
            ecs_add_pair(world, drone, EcsChildOf, ore_drone_root);
            ecs_set(world, drone, FlecsPosition3, {p[0], p[1], p[2]});
            ecs_set(world, drone, FlecsRotation3, {0, 0, 0});
            ecs_set(world, drone, FlecsScale3, {1, 1, 1});
            ecs_set(world, drone, OreDrone, {
                .pad = pad,
                .bay = b,
                .state = OreDroneStateDocked,
                .phase = oreRandf() * 6.2831853f,
                .scan = bay->scan * oreRandf()
            });
        }
    }
}

static void OreDrones(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    OreDrone *drone = ecs_field(it, OreDrone, 0);
    FlecsPosition3 *pos = ecs_field(it, FlecsPosition3, 1);
    FlecsRotation3 *rot = ecs_field(it, FlecsRotation3, 2);
    const OreSpeed *speed = ecs_field(it, OreSpeed, 3);
    const OreRange *range = ecs_field(it, OreRange, 4);
    const OreRepairRate *rate = ecs_field(it, OreRepairRate, 5);
    const OreFlight *flight = ecs_field(it, OreFlight, 6);
    const OrePowerState *power = ecs_field(it, OrePowerState, 7);

    ecs_entity_t iron = oreDroneIron(world);
    bool has_iron = iron ? oreInventoryGet(world, iron) > 0 : false;
    float dt = it->delta_time;

    for (int i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        OreDrone *d = &drone[i];

        const OreDroneBay *dock = d->pad && ecs_is_alive(world, d->pad)
            ? ecs_get(world, d->pad, OreDroneBay)
            : NULL;
        if (!dock) {
            ecs_delete(world, e);
            continue;
        }

        const FlecsPosition3 *pad = ecs_get(world, d->pad, FlecsPosition3);
        if (!pad) {
            continue;
        }

        oreDroneCache(world, e, d);

        d->phase += dt * flight[i].bob_rate;

        float bay[3];
        oreDroneBayPos(pad, d->bay, dock->count, dock->radius, dock->dock, bay);

        bool ready = !power->blackout && has_iron;
        float reach = range ? range[i].value : 0;

        bool busy = d->state == OreDroneStateFlying ||
            d->state == OreDroneStateRepairing;

        if (!oreDroneDamaged(world, d->target)) {
            d->target = 0;
        }

        if (!ready) {
            d->target = 0;

            if (busy) {
                d->state = OreDroneStateReturning;
            }
        } else if (busy && !d->target) {
            d->state = oreDroneTake(world, e, d,
                    d->state == OreDroneStateDocked ? pad : &pos[i], reach)
                ? OreDroneStateFlying
                : OreDroneStateReturning;

            d->scan = dock->scan;
        }

        if (d->state == OreDroneStateDocked ||
            d->state == OreDroneStateReturning)
        {
            if (d->scan > 0) {
                d->scan -= dt;
            }

            if (ready && d->scan <= 0) {
                d->scan = dock->scan;

                if (oreDroneTake(world, e, d,
                    d->state == OreDroneStateDocked ? pad : &pos[i], reach)) {
                    d->state = OreDroneStateFlying;
                }
            }
        }

        OreHealth *health = NULL;
        const FlecsPosition3 *tpos = NULL;

        if (d->target) {
            health = ecs_get_mut(world, d->target, OreHealth);
            tpos = ecs_get(world, d->target, FlecsPosition3);
        }

        if (!health || !tpos) {
            d->target = 0;

            if (d->state == OreDroneStateFlying ||
                d->state == OreDroneStateRepairing)
            {
                d->state = OreDroneStateReturning;
            }
        }

        float goal[3] = {bay[0], bay[1], bay[2]};

        switch (d->state) {
        case OreDroneStateDocked:
            break;
        case OreDroneStateFlying:
            goal[0] = tpos->x;
            goal[1] = flight[i].cruise;
            goal[2] = tpos->z;

            {
                float dx = goal[0] - pos[i].x;
                float dy = goal[1] - pos[i].y;
                float dz = goal[2] - pos[i].z;

                if (dx * dx + dy * dy + dz * dz < flight[i].arrive * flight[i].arrive) {
                    d->state = OreDroneStateRepairing;
                }
            }
            break;
        case OreDroneStateRepairing: {
            goal[0] = tpos->x;
            goal[1] = flight[i].cruise + sinf(d->phase) * flight[i].bob;
            goal[2] = tpos->z;

            float heal = rate[i].value * dt;
            float room = health->max - health->value;

            if (heal > room) {
                heal = room;
            }

            health->value += heal;
            ecs_modified(world, d->target, OreHealth);
            d->credit += heal;

            while (rate[i].per_iron > 0 && d->credit >= rate[i].per_iron) {
                if (!oreInventoryAdd(world, iron, -1)) {
                    has_iron = false;
                    break;
                }

                d->credit -= rate[i].per_iron;
                has_iron = oreInventoryGet(world, iron) > 0;
            }

            if (health->value >= health->max) {
                d->target = 0;

                if (!power->blackout && has_iron &&
                    oreDroneTake(world, e, d,
                    d->state == OreDroneStateDocked ? pad : &pos[i], reach))
                {
                    d->state = OreDroneStateFlying;
                } else {
                    d->state = OreDroneStateReturning;
                }

                d->scan = dock->scan;
            }
            break;
        }
        case OreDroneStateReturning: {
            float dx = bay[0] - pos[i].x;
            float dy = bay[1] - pos[i].y;
            float dz = bay[2] - pos[i].z;

            if (dx * dx + dy * dy + dz * dz < flight[i].arrive * flight[i].arrive &&
                dy * dy < 0.01f)
            {
                pos[i].x = bay[0];
                pos[i].y = bay[1];
                pos[i].z = bay[2];
                d->state = OreDroneStateDocked;
                d->scan = dock->scan * oreRandf();
            }
            break;
        }
        }

        if (d->state == OreDroneStateReturning) {
            float dx = goal[0] - pos[i].x;
            float dz = goal[2] - pos[i].z;

            if (dx * dx + dz * dz > flight[i].arrive * flight[i].arrive) {
                goal[1] = flight[i].cruise;
            }
        }

        oreDroneMove(&pos[i], &rot[i], goal, speed[i].value, &flight[i], dt);
        oreDroneSpin(world, d, &flight[i], dt);
        oreFxToggle(world, d->fx, d->state == OreDroneStateRepairing);
    }
}

void oreDronesImport(ecs_world_t *world) {
    ECS_META_COMPONENT(world, OreDroneState);
    ECS_META_COMPONENT(world, OreDroneBay);
    ECS_META_COMPONENT(world, OreRepairRate);
    ECS_META_COMPONENT(world, OreFlight);
    ECS_META_COMPONENT(world, OreDrone);

    ecs_add_pair(world, ecs_id(OreDroneBay), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(OreDrone), EcsWith, FlecsDynamicTransform);

    ecs_entity_t scope = ecs_set_scope(world, 0);
    ore_drone_root = ecs_entity(world, { .name = "drones" });
    ecs_set_scope(world, scope);

    ore_drone_query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "OreDroneQuery" }),
        .terms = {
            { .id = ecs_id(OreDrone), .inout = EcsIn }
        }
    });

    ore_repair_query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "OreRepairQuery" }),
        .terms = {
            { .id = ecs_id(OreBuilding), .inout = EcsInOutNone },
            { .id = ecs_id(OreHealth), .inout = EcsIn },
            { .id = ecs_id(FlecsPosition3), .inout = EcsIn }
        }
    });

    ecs_entity_t playing = ecs_constant_to_entity(
        world, OreGameState, OreGameStatePlaying);

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "OreDronePadOnSet" }),
        .query.terms = {
            { .id = ecs_id(OreBuilding) },
            { .id = EcsPrefab, .oper = EcsNot }
        },
        .events = { EcsOnSet },
        .callback = OreDronePadOnSet
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreDrones" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreDrone) },
            { .id = ecs_id(FlecsPosition3) },
            { .id = ecs_id(FlecsRotation3) },
            { .id = ecs_id(OreSpeed), .inout = EcsIn },
            { .id = ecs_id(OreRange), .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(OreRepairRate), .inout = EcsIn },
            { .id = ecs_id(OreFlight), .inout = EcsIn },
            { .id = ecs_id(OrePowerState), .inout = EcsIn },
            { .id = ecs_pair_t(OreGameState, playing) }
        },
        .callback = OreDrones
    });
}
