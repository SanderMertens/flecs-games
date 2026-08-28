#ifndef ORE_ELSE_UI_H
#define ORE_ELSE_UI_H

#include <flecs.h>
#include <flecs_engine.h>

#undef ECS_META_IMPL
#ifndef ORE_ELSE_UI_IMPL
#define ECS_META_IMPL EXTERN
#endif

#define ORE_UI_TABS (2)

ECS_STRUCT(OreStockSlot, {
    ecs_entity_t item;
    ecs_entity_t icon;
    int32_t count;
    bool place;
    bool active;
});

ECS_STRUCT(OreSlot, {
    ecs_entity_t item;
    int32_t order;
    bool craft;
    bool place;
    bool cancel;
});

ECS_STRUCT(OreTab, {
    int32_t index;
});

ECS_STRUCT(OreAfford, {
    bool value;
});

ECS_STRUCT(OreUiState, {
    ecs_entity_t hover_item;
    ecs_entity_t hover_recipe;
    OreStockSlot slots[15];
    OreStorageMap hover_raw;
    OreStorageMap hover_have;
    int32_t hover_raw_count;
    char *hover_name;
    char *hover_note;
    char *hover_detail;
    char *toast_text;
    float toast;
    bool toast_warn;
    bool open;
    bool hovering;
    int32_t tab;
});

void oreToast(
    ecs_world_t *world,
    const char *text,
    bool warn);

void oreAffordSet(
    ecs_world_t *world,
    ecs_entity_t widget,
    bool value);

void oreSlotClick(
    ecs_world_t *world,
    ecs_entity_t widget);

void oreTabClick(
    ecs_world_t *world,
    ecs_entity_t widget);

void oreUiImport(ecs_world_t *world);

#endif
