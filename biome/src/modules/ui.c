#include "biome.h"

static void biome_ui_toolButtonUp(
    ecs_world_t *world,
    ecs_entity_t widget,
    void *ctx)
{
    (void)widget;
    biomeToolPlaceBuilding(world, (ecs_entity_t)(uintptr_t)ctx);
}

static void biome_ui_dozeButtonUp(
    ecs_world_t *world,
    ecs_entity_t widget,
    void *ctx)
{
    (void)widget;
    (void)ctx;
    biomeToolDoze(world);
}

static void biome_ui_bindDozeButton(
    ecs_world_t *world,
    ecs_entity_t button)
{
    if (ecs_has(world, button, FlecsUiEventListener)) {
        return;
    }

    ecs_set(world, button, FlecsUiEventListener, {
        .on_lmb_up = biome_ui_dozeButtonUp
    });
}

static void biome_ui_bindToolButton(
    ecs_world_t *world,
    ecs_entity_t button,
    ecs_entity_t building)
{
    if (ecs_has(world, button, FlecsUiEventListener)) {
        return;
    }

    ecs_set(world, button, FlecsUiEventListener, {
        .on_lmb_up = biome_ui_toolButtonUp,
        .ctx = (void*)(uintptr_t)building
    });
}

void BiomeUiBind(ecs_iter_t *it) {
    ecs_world_t *world = it->world;
    ecs_entity_t tool_button = ecs_lookup(
        world, "cfg.widgets.ToolButton");
    if (!tool_button) {
        return;
    }

    ecs_member_t *building_member = ecs_struct_get_member(
        world, tool_button, "building");
    if (!building_member) {
        return;
    }

    ecs_entity_t button_type = ecs_lookup(
        world, "cfg.widgets.Button");
    ecs_entity_t button_mut = button_type
        ? ecs_lookup_child(world, button_type, "mut")
        : 0;
    ecs_member_t *active_member = button_mut
        ? ecs_struct_get_member(world, button_mut, "active")
        : NULL;
    const BiomeTool *tool = ecs_singleton_get(world, BiomeTool);
    ecs_iter_t buttons = ecs_each_id(world, tool_button);
    while (ecs_each_next(&buttons)) {
        for (int32_t i = 0; i < buttons.count; i ++) {
            ecs_entity_t button = buttons.entities[i];
            const void *data = ecs_get_id(world, button, tool_button);
            if (!data) {
                continue;
            }

            ecs_entity_t building = *(const ecs_entity_t*)ECS_OFFSET(
                data, building_member->offset);
            const void *mut_data = active_member
                ? ecs_get_id(world, button, button_mut)
                : NULL;
            if (mut_data) {
                const bool *active = ECS_OFFSET(
                    mut_data, active_member->offset);
                bool value = tool && (building
                    ? tool->building == building
                    : tool->doze);
                if (*active != value) {
                    mut_data = ecs_get_mut_id(
                        world, button, button_mut);
                    bool *active_mut = ECS_OFFSET(
                        mut_data, active_member->offset);
                    *active_mut = value;
                    ecs_modified_id(world, button, button_mut);
                }
            }

            if (building && ecs_is_alive(world, building)) {
                biome_ui_bindToolButton(world, button, building);
            } else if (!building) {
                biome_ui_bindDozeButton(world, button);
            }
        }
    }
}

void biomeUiImport(ecs_world_t *world) {
    ECS_MODULE(world, biomeUi);

    ECS_SYSTEM(world, BiomeUiBind, EcsOnUpdate, 0);
}
