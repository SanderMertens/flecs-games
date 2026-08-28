#define ORE_ELSE_POWER_IMPL
#include "ore_else.h"

#include <stdio.h>

static int32_t orePowerBars(ecs_world_t *world) {
    return ecs_const_var_get_t(world, "cfg.powerBars", ecs_i32_t);
}

static int32_t orePowerAssigned(const OrePowerAlloc *alloc) {
    return alloc->level[0] + alloc->level[1] + alloc->level[2];
}

int32_t orePowerLevel(ecs_world_t *world, ecs_entity_t e) {
    if (!e) {
        return -1;
    }

    const OrePowerCategory *cat = ecs_get(world, e, OrePowerCategory);
    if (!cat) {
        return -1;
    }

    const OrePowerAlloc *alloc = ecs_singleton_get(world, OrePowerAlloc);
    if (!alloc || cat->kind < 0 || cat->kind > OrePowerKindConstruction) {
        return -1;
    }

    return alloc->level[cat->kind];
}

float orePowerMul(ecs_world_t *world, ecs_entity_t e) {
    int32_t level = orePowerLevel(world, e);
    if (level < 0) {
        return 1.0f;
    }

    return (float)level *
        ecs_const_var_get_t(world, "cfg.powerStep", ecs_f32_t);
}

static void OrePower(ecs_iter_t *it) {
    ecs_world_t *world = it->world;

    OrePowerState *power = ecs_field(it, OrePowerState, 0);

    float prod = 0, demand = 0;

    ecs_iter_t pit = ecs_query_iter(world, power->producer_query);
    while (ecs_query_next(&pit)) {
        const OrePowerProducer *p = ecs_field(&pit, OrePowerProducer, 0);
        for (int i = 0; i < pit.count; i ++) {
            prod += p[i].watts;
        }
    }

    ecs_iter_t cit = ecs_query_iter(world, power->consumer_query);
    while (ecs_query_next(&cit)) {
        const OrePowerConsumer *c = ecs_field(&cit, OrePowerConsumer, 0);
        for (int i = 0; i < cit.count; i ++) {
            demand += c[i].watts;
        }
    }

    const OrePowerAlloc *alloc = ecs_singleton_get(world, OrePowerAlloc);
    int32_t bars = orePowerBars(world);

    if (alloc && bars > 0) {
        demand = demand * (float)orePowerAssigned(alloc) / (float)bars;
    }

    float ratio = 1.0f;

    if (demand > 0 && prod < demand) {
        ratio = prod / demand;
    }

    float at = ecs_const_var_get_t(world, "cfg.blackoutAt", ecs_f32_t);
    bool blackout = demand > 0 && ratio < at;

    if (power->prod == prod && power->demand == demand &&
        power->ratio == ratio && power->blackout == blackout)
    {
        return;
    }

    power->prod = prod;
    power->demand = demand;
    power->ratio = ratio;
    power->blackout = blackout;

    ecs_singleton_modified(world, OrePowerState);
}

void orePowerSegClick(
    ecs_world_t *world,
    ecs_entity_t widget)
{
    if (!orePlaying(world)) {
        return;
    }

    const OrePowerSeg *seg = ecs_get(world, widget, OrePowerSeg);
    if (!seg) {
        return;
    }

    if (seg->kind < 0 || seg->kind > OrePowerKindConstruction) {
        return;
    }

    OrePowerAlloc *alloc = ecs_singleton_ensure(world, OrePowerAlloc);
    int32_t level = alloc->level[seg->kind];

    int32_t next = (level == seg->level) ? seg->level - 1 : seg->level;
    if (next < 0 || next == level) {
        return;
    }

    if (next > level) {
        int32_t bars = orePowerBars(world);
        int32_t headroom = bars - (orePowerAssigned(alloc) - level);

        if (next > headroom) {
            next = headroom;
        }

        if (next <= level) {
            char buf[64];
            snprintf(buf, sizeof(buf),
                "Only %d power bars - free one somewhere else", bars);

            oreToast(world, buf, true);
            return;
        }
    }

    alloc->level[seg->kind] = next;

    ecs_singleton_modified(world, OrePowerAlloc);
}

void orePowerImport(ecs_world_t *world) {
    ECS_META_COMPONENT(world, OrePowerProducer);
    ECS_META_COMPONENT(world, OrePowerConsumer);
    ECS_META_COMPONENT(world, OrePowerKind);
    ECS_META_COMPONENT(world, OrePowerCategory);
    ECS_META_COMPONENT(world, OrePowerAlloc);
    ECS_META_COMPONENT(world, OrePowerSeg);
    ECS_META_COMPONENT(world, OrePowerState);

    ecs_add_id(world, ecs_id(OrePowerState), EcsSingleton);

    ecs_add_pair(world, ecs_id(OrePowerCategory), EcsOnInstantiate, EcsInherit);

    ecs_add_pair(world, ecs_id(OrePowerSeg), EcsWith,
        ecs_id(FlecsUiWidgetState));

    OrePowerState *power = ecs_singleton_ensure(world, OrePowerState);

    power->producer_query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "OreProducerQuery" }),
        .terms = {
            { .id = ecs_id(OrePowerProducer), .src.id = EcsSelf }
        }
    });

    power->consumer_query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "OreConsumerQuery" }),
        .terms = {
            { .id = ecs_id(OrePowerConsumer), .src.id = EcsSelf }
        }
    });

    ecs_singleton_modified(world, OrePowerState);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OrePower" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OrePowerState) }
        },
        .callback = OrePower
    });

}
