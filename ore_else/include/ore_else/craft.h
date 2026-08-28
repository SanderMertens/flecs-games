#ifndef ORE_ELSE_CRAFT_H
#define ORE_ELSE_CRAFT_H

#include <flecs.h>
#include <flecs_engine.h>

#undef ECS_META_IMPL
#ifndef ORE_ELSE_CRAFT_IMPL
#define ECS_META_IMPL EXTERN
#endif

#define ORE_CRAFT_ORDERS (8)
#define ORE_CRAFT_ITEMS (8)
#define ORE_CRAFT_HOLD (12)
#define ORE_CRAFT_BAG (24)
#define ORE_CRAFT_ROWS (16)
#define ORE_CRAFT_DEPTH (8)
#define ORE_CRAFT_BATCH (5)

ECS_STRUCT(OreCrafter, {
    ecs_entity_t item;
    int32_t order;
    float left;
    float total;
    float speed;
});

ECS_STRUCT(OreCraftEntry, {
    ecs_entity_t item;
    int32_t count;
    int32_t running;
    char *text;
    bool stalled;
    bool full;
});

ECS_STRUCT(OreCraftBag, {
    ecs_entity_t items[24];
    int32_t amounts[24];
    int32_t count;
});

ECS_STRUCT(OreCraftOrder, {
    ecs_entity_t item;
    int32_t id;
    int32_t count;
    int32_t entry_count;
    OreCraftEntry entries[8];
    OreCraftBag pool;
    bool stalled;
    bool full;
});

ECS_STRUCT(OreCraftRow, {
    ecs_entity_t item;
    ecs_entity_t icon;
    char *text;
    float progress;
    int32_t order;
    int32_t slot;
    bool active;
    bool stalled;
    bool cancel;
    bool sub;
});

ECS_STRUCT(OreCraftState, {
    OreCraftOrder orders[8];
    OreCraftRow rows[16];
    int32_t row_count;
    int32_t count;
    int32_t next_id;
    bool stalled;

ECS_PRIVATE
    ecs_query_t *crafter_query;
});

bool oreCrafterBusy(
    const OreCrafter *crafter);

bool oreCraftEnqueue(
    ecs_world_t *world,
    ecs_entity_t item,
    bool merge);

int32_t oreCraftRawCost(
    ecs_world_t *world,
    const OreCraftState *craft,
    ecs_entity_t item,
    OreStorageMap *dst,
    OreStorageMap *have);

bool oreCraftReachable(
    ecs_world_t *world,
    const OreCraftState *craft,
    ecs_entity_t item,
    bool queue);

bool oreCraftCancelOrder(
    ecs_world_t *world,
    int32_t order);

void oreCraftClear(
    ecs_world_t *world,
    OreCraftState *craft);

void oreCraftImport(ecs_world_t *world);

#endif
